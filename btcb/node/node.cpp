#include <btcb/node/node.hpp>

#include <btcb/crypto_lib/random_pool.hpp>
#include <btcb/lib/interface.h>
#include <btcb/lib/timer.hpp>
#include <btcb/lib/utility.hpp>
#include <btcb/node/common.hpp>
#include <btcb/rpc/rpc.hpp>

#include <algorithm>
#include <cstdlib>
#include <future>
#include <numeric>
#include <sstream>

#include <boost/polymorphic_cast.hpp>
#include <boost/property_tree/json_parser.hpp>

double constexpr btcb::node::price_max;
double constexpr btcb::node::free_cutoff;
size_t constexpr btcb::block_arrival::arrival_size_min;
std::chrono::seconds constexpr btcb::block_arrival::arrival_time_min;

namespace btcb
{
extern unsigned char btcb_bootstrap_weights_live[];
extern size_t btcb_bootstrap_weights_live_size;
extern unsigned char btcb_bootstrap_weights_beta[];
extern size_t btcb_bootstrap_weights_beta_size;
}

btcb::network::network (btcb::node & node_a, uint16_t port_a) :
buffer_container (node_a.stats, btcb::network::buffer_size, 4096), // 2Mb receive buffer
resolver (node_a.io_ctx),
node (node_a),
udp_channels (node_a, port_a),
tcp_channels (node_a),
disconnect_observer ([]() {})
{
	boost::thread::attributes attrs;
	btcb::thread_attributes::set (attrs);
	for (size_t i = 0; i < node.config.network_threads; ++i)
	{
		packet_processing_threads.push_back (boost::thread (attrs, [this]() {
			btcb::thread_role::set (btcb::thread_role::name::packet_processing);
			try
			{
				udp_channels.process_packets ();
			}
			catch (boost::system::error_code & ec)
			{
				this->node.logger.try_log (FATAL_LOG_PREFIX, ec.message ());
				release_assert (false);
			}
			catch (std::error_code & ec)
			{
				this->node.logger.try_log (FATAL_LOG_PREFIX, ec.message ());
				release_assert (false);
			}
			catch (std::runtime_error & err)
			{
				this->node.logger.try_log (FATAL_LOG_PREFIX, err.what ());
				release_assert (false);
			}
			catch (...)
			{
				this->node.logger.try_log (FATAL_LOG_PREFIX, "Unknown exception");
				release_assert (false);
			}
			if (this->node.config.logging.network_packet_logging ())
			{
				this->node.logger.try_log ("Exiting packet processing thread");
			}
		}));
	}
}

btcb::network::~network ()
{
	for (auto & thread : packet_processing_threads)
	{
		thread.join ();
	}
}

void btcb::network::start ()
{
	ongoing_cleanup ();
	udp_channels.start ();
	tcp_channels.start ();
}

void btcb::network::stop ()
{
	udp_channels.stop ();
	tcp_channels.stop ();
	resolver.cancel ();
	buffer_container.stop ();
}

void btcb::network::send_keepalive (std::shared_ptr<btcb::transport::channel> channel_a)
{
	btcb::keepalive message;
	random_fill (message.peers);
	channel_a->send (message);
}

void btcb::network::send_keepalive_self (std::shared_ptr<btcb::transport::channel> channel_a)
{
	btcb::keepalive message;
	if (node.config.external_address != boost::asio::ip::address_v6{} && node.config.external_port != 0)
	{
		message.peers[0] = btcb::endpoint (node.config.external_address, node.config.external_port);
	}
	else
	{
		auto external_address (node.port_mapping.external_address ());
		if (external_address.address () != boost::asio::ip::address_v4::any ())
		{
			message.peers[0] = btcb::endpoint (boost::asio::ip::address_v6{}, endpoint ().port ());
			message.peers[1] = external_address;
		}
		else
		{
			message.peers[0] = btcb::endpoint (boost::asio::ip::address_v6{}, endpoint ().port ());
		}
	}
	channel_a->send (message);
}

void btcb::node::keepalive (std::string const & address_a, uint16_t port_a)
{
	auto node_l (shared_from_this ());
	network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (address_a, std::to_string (port_a)), [node_l, address_a, port_a](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
		if (!ec)
		{
			for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
			{
				auto endpoint (btcb::transport::map_endpoint_to_v6 (i->endpoint ()));
				std::weak_ptr<btcb::node> node_w (node_l);
				auto channel (node_l->network.find_channel (endpoint));
				if (!channel)
				{
					node_l->network.tcp_channels.start_tcp (endpoint, [node_w](std::shared_ptr<btcb::transport::channel> channel_a) {
						if (auto node_l = node_w.lock ())
						{
							node_l->network.send_keepalive (channel_a);
						}
					});
				}
				else
				{
					node_l->network.send_keepalive (channel);
				}
			}
		}
		else
		{
			node_l->logger.try_log (boost::str (boost::format ("Error resolving address: %1%:%2%: %3%") % address_a % port_a % ec.message ()));
		}
	});
}

void btcb::network::send_node_id_handshake (std::shared_ptr<btcb::transport::channel> channel_a, boost::optional<btcb::uint256_union> const & query, boost::optional<btcb::uint256_union> const & respond_to)
{
	boost::optional<std::pair<btcb::account, btcb::signature>> response (boost::none);
	if (respond_to)
	{
		response = std::make_pair (node.node_id.pub, btcb::sign_message (node.node_id.prv, node.node_id.pub, *respond_to));
		assert (!btcb::validate_message (response->first, *respond_to, response->second));
	}
	btcb::node_id_handshake message (query, response);
	if (node.config.logging.network_node_id_handshake_logging ())
	{
		node.logger.try_log (boost::str (boost::format ("Node ID handshake sent with node ID %1% to %2%: query %3%, respond_to %4% (signature %5%)") % node.node_id.pub.to_account () % channel_a->get_endpoint () % (query ? query->to_string () : std::string ("[none]")) % (respond_to ? respond_to->to_string () : std::string ("[none]")) % (response ? response->second.to_string () : std::string ("[none]"))));
	}
	channel_a->send (message);
}

template <typename T>
bool confirm_block (btcb::transaction const & transaction_a, btcb::node & node_a, T & list_a, std::shared_ptr<btcb::block> block_a, bool also_publish)
{
	bool result (false);
	if (node_a.config.enable_voting)
	{
		auto hash (block_a->hash ());
		// Search in cache
		auto votes (node_a.votes_cache.find (hash));
		if (votes.empty ())
		{
			// Generate new vote
			node_a.wallets.foreach_representative (transaction_a, [&result, &list_a, &node_a, &transaction_a, &hash](btcb::public_key const & pub_a, btcb::raw_key const & prv_a) {
				result = true;
				auto vote (node_a.store.vote_generate (transaction_a, pub_a, prv_a, std::vector<btcb::block_hash> (1, hash)));
				btcb::confirm_ack confirm (vote);
				auto vote_bytes = confirm.to_bytes ();
				for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
				{
					j->get ()->send_buffer (vote_bytes, btcb::stat::detail::confirm_ack);
				}
				node_a.votes_cache.add (vote);
			});
		}
		else
		{
			// Send from cache
			for (auto & vote : votes)
			{
				btcb::confirm_ack confirm (vote);
				auto vote_bytes = confirm.to_bytes ();
				for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
				{
					j->get ()->send_buffer (vote_bytes, btcb::stat::detail::confirm_ack);
				}
			}
		}
		// Republish if required
		if (also_publish)
		{
			btcb::publish publish (block_a);
			auto publish_bytes (publish.to_bytes ());
			for (auto j (list_a.begin ()), m (list_a.end ()); j != m; ++j)
			{
				j->get ()->send_buffer (publish_bytes, btcb::stat::detail::publish);
			}
		}
	}
	return result;
}

bool confirm_block (btcb::transaction const & transaction_a, btcb::node & node_a, std::shared_ptr<btcb::transport::channel> channel_a, std::shared_ptr<btcb::block> block_a, bool also_publish)
{
	std::array<std::shared_ptr<btcb::transport::channel>, 1> endpoints = { channel_a };
	auto result (confirm_block (transaction_a, node_a, endpoints, std::move (block_a), also_publish));
	return result;
}

void btcb::network::confirm_hashes (btcb::transaction const & transaction_a, std::shared_ptr<btcb::transport::channel> channel_a, std::vector<btcb::block_hash> blocks_bundle_a)
{
	if (node.config.enable_voting)
	{
		node.wallets.foreach_representative (transaction_a, [this, &blocks_bundle_a, &channel_a, &transaction_a](btcb::public_key const & pub_a, btcb::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction_a, pub_a, prv_a, blocks_bundle_a));
			btcb::confirm_ack confirm (vote);
			std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
			{
				btcb::vectorstream stream (*bytes);
				confirm.serialize (stream);
			}
			channel_a->send_buffer (bytes, btcb::stat::detail::confirm_ack);
			this->node.votes_cache.add (vote);
		});
	}
}

bool btcb::network::send_votes_cache (std::shared_ptr<btcb::transport::channel> channel_a, btcb::block_hash const & hash_a)
{
	// Search in cache
	auto votes (node.votes_cache.find (hash_a));
	// Send from cache
	for (auto & vote : votes)
	{
		btcb::confirm_ack confirm (vote);
		auto vote_bytes = confirm.to_bytes ();
		channel_a->send_buffer (vote_bytes, btcb::stat::detail::confirm_ack);
	}
	// Returns true if votes were sent
	bool result (!votes.empty ());
	return result;
}

void btcb::network::flood_message (btcb::message const & message_a)
{
	auto list (list_fanout ());
	for (auto i (list.begin ()), n (list.end ()); i != n; ++i)
	{
		(*i)->send (message_a);
	}
}

void btcb::network::flood_block_batch (std::deque<std::shared_ptr<btcb::block>> blocks_a, unsigned delay_a)
{
	auto block (blocks_a.front ());
	blocks_a.pop_front ();
	flood_block (block);
	if (!blocks_a.empty ())
	{
		std::weak_ptr<btcb::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a + std::rand () % delay_a), [node_w, blocks_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.flood_block_batch (blocks_a, delay_a);
			}
		});
	}
}

void btcb::network::broadcast_confirm_req (std::shared_ptr<btcb::block> block_a)
{
	auto list (std::make_shared<std::vector<std::shared_ptr<btcb::transport::channel>>> (node.rep_crawler.representative_endpoints (std::numeric_limits<size_t>::max ())));
	if (list->empty () || node.rep_crawler.total_weight () < node.config.online_weight_minimum.number ())
	{
		// broadcast request to all peers (with max limit 2 * sqrt (peers count))
		auto peers (node.network.list (std::min (static_cast<size_t> (100), 2 * node.network.size_sqrt ())));
		list->clear ();
		for (auto & peer : peers)
		{
			list->push_back (peer);
		}
	}

	/*
	 * In either case (broadcasting to all representatives, or broadcasting to
	 * all peers because there are not enough connected representatives),
	 * limit each instance to a single random up-to-32 selection.  The invoker
	 * of "broadcast_confirm_req" will be responsible for calling it again
	 * if the votes for a block have not arrived in time.
	 */
	const size_t max_endpoints = 32;
	random_pool::shuffle (list->begin (), list->end ());
	if (list->size () > max_endpoints)
	{
		list->erase (list->begin () + max_endpoints, list->end ());
	}

	broadcast_confirm_req_base (block_a, list, 0);
}

void btcb::network::broadcast_confirm_req_base (std::shared_ptr<btcb::block> block_a, std::shared_ptr<std::vector<std::shared_ptr<btcb::transport::channel>>> endpoints_a, unsigned delay_a, bool resumption)
{
	const size_t max_reps = 10;
	if (!resumption && node.config.logging.network_logging ())
	{
		node.logger.try_log (boost::str (boost::format ("Broadcasting confirm req for block %1% to %2% representatives") % block_a->hash ().to_string () % endpoints_a->size ()));
	}
	auto count (0);
	while (!endpoints_a->empty () && count < max_reps)
	{
		btcb::confirm_req req (block_a);
		auto channel (endpoints_a->back ());
		channel->send (req);
		endpoints_a->pop_back ();
		count++;
	}
	if (!endpoints_a->empty ())
	{
		delay_a += std::rand () % broadcast_interval_ms;

		std::weak_ptr<btcb::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a), [node_w, block_a, endpoints_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.broadcast_confirm_req_base (block_a, endpoints_a, delay_a, true);
			}
		});
	}
}

void btcb::network::broadcast_confirm_req_batch (std::unordered_map<std::shared_ptr<btcb::transport::channel>, std::vector<std::pair<btcb::block_hash, btcb::block_hash>>> request_bundle_a, unsigned delay_a, bool resumption)
{
	const size_t max_reps = 10;
	if (!resumption && node.config.logging.network_logging ())
	{
		node.logger.try_log (boost::str (boost::format ("Broadcasting batch confirm req to %1% representatives") % request_bundle_a.size ()));
	}
	auto count (0);
	while (!request_bundle_a.empty () && count < max_reps)
	{
		auto j (request_bundle_a.begin ());
		count++;
		std::vector<std::pair<btcb::block_hash, btcb::block_hash>> roots_hashes;
		// Limit max request size hash + root to 6 pairs
		while (roots_hashes.size () <= confirm_req_hashes_max && !j->second.empty ())
		{
			roots_hashes.push_back (j->second.back ());
			j->second.pop_back ();
		}
		btcb::confirm_req req (roots_hashes);
		j->first->send (req);
		if (j->second.empty ())
		{
			request_bundle_a.erase (j);
		}
	}
	if (!request_bundle_a.empty ())
	{
		std::weak_ptr<btcb::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a), [node_w, request_bundle_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.broadcast_confirm_req_batch (request_bundle_a, delay_a + 50, true);
			}
		});
	}
}

void btcb::network::broadcast_confirm_req_batch (std::deque<std::pair<std::shared_ptr<btcb::block>, std::shared_ptr<std::vector<std::shared_ptr<btcb::transport::channel>>>>> deque_a, unsigned delay_a)
{
	auto pair (deque_a.front ());
	deque_a.pop_front ();
	auto block (pair.first);
	// confirm_req to representatives
	auto endpoints (pair.second);
	if (!endpoints->empty ())
	{
		broadcast_confirm_req_base (block, endpoints, delay_a);
	}
	/* Continue while blocks remain
	Broadcast with random delay between delay_a & 2*delay_a */
	if (!deque_a.empty ())
	{
		std::weak_ptr<btcb::node> node_w (node.shared ());
		node.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (delay_a + std::rand () % delay_a), [node_w, deque_a, delay_a]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.broadcast_confirm_req_batch (deque_a, delay_a);
			}
		});
	}
}

namespace
{
class network_message_visitor : public btcb::message_visitor
{
public:
	network_message_visitor (btcb::node & node_a, std::shared_ptr<btcb::transport::channel> channel_a) :
	node (node_a),
	channel (channel_a)
	{
	}
	void keepalive (btcb::keepalive const & message_a) override
	{
		if (node.config.logging.network_keepalive_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Received keepalive message from %1%") % channel->to_string ()));
		}
		node.stats.inc (btcb::stat::type::message, btcb::stat::detail::keepalive, btcb::stat::dir::in);
		node.network.merge_peers (message_a.peers);
	}
	void publish (btcb::publish const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Publish message from %1% for %2%") % channel->to_string () % message_a.block->hash ().to_string ()));
		}
		node.stats.inc (btcb::stat::type::message, btcb::stat::detail::publish, btcb::stat::dir::in);
		if (!node.block_processor.full ())
		{
			node.process_active (message_a.block);
		}
		node.active.publish (message_a.block);
	}
	void confirm_req (btcb::confirm_req const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			if (!message_a.roots_hashes.empty ())
			{
				node.logger.try_log (boost::str (boost::format ("Confirm_req message from %1% for hashes:roots %2%") % channel->to_string () % message_a.roots_string ()));
			}
			else
			{
				node.logger.try_log (boost::str (boost::format ("Confirm_req message from %1% for %2%") % channel->to_string () % message_a.block->hash ().to_string ()));
			}
		}
		node.stats.inc (btcb::stat::type::message, btcb::stat::detail::confirm_req, btcb::stat::dir::in);
		// Don't load nodes with disabled voting
		if (node.config.enable_voting && node.wallets.reps_count)
		{
			if (message_a.block != nullptr)
			{
				auto hash (message_a.block->hash ());
				if (!node.network.send_votes_cache (channel, hash))
				{
					auto transaction (node.store.tx_begin_read ());
					auto successor (node.ledger.successor (transaction, message_a.block->qualified_root ()));
					if (successor != nullptr)
					{
						auto same_block (successor->hash () == hash);
						confirm_block (transaction, node, channel, std::move (successor), !same_block);
					}
				}
			}
			else if (!message_a.roots_hashes.empty ())
			{
				auto transaction (node.store.tx_begin_read ());
				std::vector<btcb::block_hash> blocks_bundle;
				for (auto & root_hash : message_a.roots_hashes)
				{
					if (!node.network.send_votes_cache (channel, root_hash.first) && node.store.block_exists (transaction, root_hash.first))
					{
						blocks_bundle.push_back (root_hash.first);
					}
					else
					{
						btcb::block_hash successor (0);
						// Search for block root
						successor = node.store.block_successor (transaction, root_hash.second);
						// Search for account root
						if (successor.is_zero () && node.store.account_exists (transaction, root_hash.second))
						{
							btcb::account_info info;
							auto error (node.store.account_get (transaction, root_hash.second, info));
							assert (!error);
							successor = info.open_block;
						}
						if (!successor.is_zero ())
						{
							if (!node.network.send_votes_cache (channel, successor))
							{
								blocks_bundle.push_back (successor);
							}
							auto successor_block (node.store.block_get (transaction, successor));
							assert (successor_block != nullptr);
							btcb::publish publish (successor_block);
							channel->send (publish);
						}
					}
				}
				if (!blocks_bundle.empty ())
				{
					node.network.confirm_hashes (transaction, channel, blocks_bundle);
				}
			}
		}
	}
	void confirm_ack (btcb::confirm_ack const & message_a) override
	{
		if (node.config.logging.network_message_logging ())
		{
			node.logger.try_log (boost::str (boost::format ("Received confirm_ack message from %1% for %2%sequence %3%") % channel->to_string () % message_a.vote->hashes_string () % std::to_string (message_a.vote->sequence)));
		}
		node.stats.inc (btcb::stat::type::message, btcb::stat::detail::confirm_ack, btcb::stat::dir::in);
		for (auto & vote_block : message_a.vote->blocks)
		{
			if (!vote_block.which ())
			{
				auto block (boost::get<std::shared_ptr<btcb::block>> (vote_block));
				if (!node.block_processor.full ())
				{
					node.process_active (block);
				}
				node.active.publish (block);
			}
		}
		node.vote_processor.vote (message_a.vote, channel);
	}
	void bulk_pull (btcb::bulk_pull const &) override
	{
		assert (false);
	}
	void bulk_pull_account (btcb::bulk_pull_account const &) override
	{
		assert (false);
	}
	void bulk_push (btcb::bulk_push const &) override
	{
		assert (false);
	}
	void frontier_req (btcb::frontier_req const &) override
	{
		assert (false);
	}
	void node_id_handshake (btcb::node_id_handshake const & message_a) override
	{
		node.stats.inc (btcb::stat::type::message, btcb::stat::detail::node_id_handshake, btcb::stat::dir::in);
	}
	btcb::node & node;
	std::shared_ptr<btcb::transport::channel> channel;
};
}

// Send keepalives to all the peers we've been notified of
void btcb::network::merge_peers (std::array<btcb::endpoint, 8> const & peers_a)
{
	for (auto i (peers_a.begin ()), j (peers_a.end ()); i != j; ++i)
	{
		merge_peer (*i);
	}
}

void btcb::network::merge_peer (btcb::endpoint const & peer_a)
{
	if (!reachout (peer_a, node.config.allow_local_peers))
	{
		std::weak_ptr<btcb::node> node_w (node.shared ());
		node.network.tcp_channels.start_tcp (peer_a, [node_w](std::shared_ptr<btcb::transport::channel> channel_a) {
			if (auto node_l = node_w.lock ())
			{
				node_l->network.send_keepalive (channel_a);
			}
		});
	}
}

bool btcb::network::not_a_peer (btcb::endpoint const & endpoint_a, bool allow_local_peers)
{
	bool result (false);
	if (endpoint_a.address ().to_v6 ().is_unspecified ())
	{
		result = true;
	}
	else if (btcb::transport::reserved_address (endpoint_a, allow_local_peers))
	{
		result = true;
	}
	else if (endpoint_a == endpoint ())
	{
		result = true;
	}
	return result;
}

bool btcb::network::reachout (btcb::endpoint const & endpoint_a, bool allow_local_peers)
{
	// Don't contact invalid IPs
	bool error = not_a_peer (endpoint_a, allow_local_peers);
	if (!error)
	{
		error |= udp_channels.reachout (endpoint_a);
		error |= tcp_channels.reachout (endpoint_a);
	}
	return error;
}

std::deque<std::shared_ptr<btcb::transport::channel>> btcb::network::list (size_t count_a)
{
	std::deque<std::shared_ptr<btcb::transport::channel>> result;
	tcp_channels.list (result);
	udp_channels.list (result);
	random_pool::shuffle (result.begin (), result.end ());
	if (result.size () > count_a)
	{
		result.resize (count_a, nullptr);
	}
	return result;
}

// Simulating with sqrt_broadcast_simulate shows we only need to broadcast to sqrt(total_peers) random peers in order to successfully publish to everyone with high probability
std::deque<std::shared_ptr<btcb::transport::channel>> btcb::network::list_fanout ()
{
	auto result (list (size_sqrt ()));
	return result;
}

std::unordered_set<std::shared_ptr<btcb::transport::channel>> btcb::network::random_set (size_t count_a) const
{
	std::unordered_set<std::shared_ptr<btcb::transport::channel>> result (tcp_channels.random_set (count_a));
	std::unordered_set<std::shared_ptr<btcb::transport::channel>> udp_random (udp_channels.random_set (count_a));
	for (auto i (udp_random.begin ()), n (udp_random.end ()); i != n && result.size () < count_a * 1.5; ++i)
	{
		result.insert (*i);
	}
	while (result.size () > count_a)
	{
		result.erase (result.begin ());
	}
	return result;
}

void btcb::network::random_fill (std::array<btcb::endpoint, 8> & target_a) const
{
	auto peers (random_set (target_a.size ()));
	assert (peers.size () <= target_a.size ());
	auto endpoint (btcb::endpoint (boost::asio::ip::address_v6{}, 0));
	assert (endpoint.address ().is_v6 ());
	std::fill (target_a.begin (), target_a.end (), endpoint);
	auto j (target_a.begin ());
	for (auto i (peers.begin ()), n (peers.end ()); i != n; ++i, ++j)
	{
		assert ((*i)->get_endpoint ().address ().is_v6 ());
		assert (j < target_a.end ());
		*j = (*i)->get_endpoint ();
	}
}

btcb::tcp_endpoint btcb::network::bootstrap_peer ()
{
	auto result (udp_channels.bootstrap_peer ());
	if (result == btcb::tcp_endpoint (boost::asio::ip::address_v6::any (), 0))
	{
		result = tcp_channels.bootstrap_peer ();
	}
	return result;
}

std::shared_ptr<btcb::transport::channel> btcb::network::find_channel (btcb::endpoint const & endpoint_a)
{
	std::shared_ptr<btcb::transport::channel> result (tcp_channels.find_channel (btcb::transport::map_endpoint_to_tcp (endpoint_a)));
	if (!result)
	{
		result = udp_channels.channel (endpoint_a);
	}
	return result;
}

std::shared_ptr<btcb::transport::channel> btcb::network::find_node_id (btcb::account const & node_id_a)
{
	std::shared_ptr<btcb::transport::channel> result (tcp_channels.find_node_id (node_id_a));
	if (!result)
	{
		result = udp_channels.find_node_id (node_id_a);
	}
	return result;
}

void btcb::network::add_response_channels (btcb::tcp_endpoint const & endpoint_a, std::vector<btcb::tcp_endpoint> insert_channels)
{
	std::lock_guard<std::mutex> lock (response_channels_mutex);
	response_channels.emplace (endpoint_a, insert_channels);
}

std::shared_ptr<btcb::transport::channel> btcb::network::search_response_channel (btcb::tcp_endpoint const & endpoint_a, btcb::account const & node_id_a)
{
	// Search by node ID
	std::shared_ptr<btcb::transport::channel> result (find_node_id (node_id_a));
	if (!result)
	{
		// Search in response channels
		std::unique_lock<std::mutex> lock (response_channels_mutex);
		auto existing (response_channels.find (endpoint_a));
		if (existing != response_channels.end ())
		{
			auto channels_list (existing->second);
			lock.unlock ();
			// TCP
			for (auto & i : channels_list)
			{
				auto search_channel (tcp_channels.find_channel (i));
				if (search_channel != nullptr)
				{
					result = search_channel;
					break;
				}
			}
			// UDP
			if (!result)
			{
				for (auto & i : channels_list)
				{
					auto udp_endpoint (btcb::transport::map_tcp_to_endpoint (i));
					auto search_channel (udp_channels.channel (udp_endpoint));
					if (search_channel != nullptr)
					{
						result = search_channel;
						break;
					}
				}
			}
		}
	}
	return result;
}

void btcb::network::remove_response_channel (btcb::tcp_endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (response_channels_mutex);
	response_channels.erase (endpoint_a);
}

size_t btcb::network::response_channels_size ()
{
	std::lock_guard<std::mutex> lock (response_channels_mutex);
	return response_channels.size ();
}

bool btcb::operation::operator> (btcb::operation const & other_a) const
{
	return wakeup > other_a.wakeup;
}

btcb::alarm::alarm (boost::asio::io_context & io_ctx_a) :
io_ctx (io_ctx_a),
thread ([this]() {
	btcb::thread_role::set (btcb::thread_role::name::alarm);
	run ();
})
{
}

btcb::alarm::~alarm ()
{
	add (std::chrono::steady_clock::now (), nullptr);
	thread.join ();
}

void btcb::alarm::run ()
{
	std::unique_lock<std::mutex> lock (mutex);
	auto done (false);
	while (!done)
	{
		if (!operations.empty ())
		{
			auto & operation (operations.top ());
			if (operation.function)
			{
				if (operation.wakeup <= std::chrono::steady_clock::now ())
				{
					io_ctx.post (operation.function);
					operations.pop ();
				}
				else
				{
					auto wakeup (operation.wakeup);
					condition.wait_until (lock, wakeup);
				}
			}
			else
			{
				done = true;
			}
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void btcb::alarm::add (std::chrono::steady_clock::time_point const & wakeup_a, std::function<void()> const & operation)
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		operations.push (btcb::operation ({ wakeup_a, operation }));
	}
	condition.notify_all ();
}

namespace btcb
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (alarm & alarm, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);
	size_t count = 0;
	{
		std::lock_guard<std::mutex> guard (alarm.mutex);
		count = alarm.operations.size ();
	}
	auto sizeof_element = sizeof (decltype (alarm.operations)::value_type);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "operations", count, sizeof_element }));
	return composite;
}
}

bool btcb::node_init::error () const
{
	return block_store_init || wallets_store_init;
}

btcb::vote_processor::vote_processor (btcb::node & node_a) :
node (node_a),
started (false),
stopped (false),
active (false),
thread ([this]() {
	btcb::thread_role::set (btcb::thread_role::name::vote_processing);
	process_loop ();
})
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

void btcb::vote_processor::process_loop ()
{
	std::chrono::steady_clock::time_point start_time, end_time;
	std::chrono::steady_clock::duration elapsed_time;
	std::chrono::milliseconds elapsed_time_ms;
	uint64_t elapsed_time_ms_int;
	bool log_this_iteration;

	std::unique_lock<std::mutex> lock (mutex);
	started = true;

	lock.unlock ();
	condition.notify_all ();
	lock.lock ();

	while (!stopped)
	{
		if (!votes.empty ())
		{
			std::deque<std::pair<std::shared_ptr<btcb::vote>, std::shared_ptr<btcb::transport::channel>>> votes_l;
			votes_l.swap (votes);

			log_this_iteration = false;
			if (node.config.logging.network_logging () && votes_l.size () > 50)
			{
				/*
				 * Only log the timing information for this iteration if
				 * there are a sufficient number of items for it to be relevant
				 */
				log_this_iteration = true;
				start_time = std::chrono::steady_clock::now ();
			}
			active = true;
			lock.unlock ();
			verify_votes (votes_l);
			{
				std::unique_lock<std::mutex> active_single_lock (node.active.mutex);
				auto transaction (node.store.tx_begin_read ());
				uint64_t count (1);
				for (auto & i : votes_l)
				{
					vote_blocking (transaction, i.first, i.second, true);
					// Free active_transactions mutex each 100 processed votes
					if (count % 100 == 0)
					{
						active_single_lock.unlock ();
						active_single_lock.lock ();
					}
					count++;
				}
			}
			lock.lock ();
			active = false;

			lock.unlock ();
			condition.notify_all ();
			lock.lock ();

			if (log_this_iteration)
			{
				end_time = std::chrono::steady_clock::now ();
				elapsed_time = end_time - start_time;
				elapsed_time_ms = std::chrono::duration_cast<std::chrono::milliseconds> (elapsed_time);
				elapsed_time_ms_int = elapsed_time_ms.count ();

				if (elapsed_time_ms_int >= 100)
				{
					/*
					 * If the time spent was less than 100ms then
					 * the results are probably not useful as well,
					 * so don't spam the logs.
					 */
					node.logger.try_log (boost::str (boost::format ("Processed %1% votes in %2% milliseconds (rate of %3% votes per second)") % votes_l.size () % elapsed_time_ms_int % ((votes_l.size () * 1000ULL) / elapsed_time_ms_int)));
				}
			}
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void btcb::vote_processor::vote (std::shared_ptr<btcb::vote> vote_a, std::shared_ptr<btcb::transport::channel> channel_a)
{
	std::unique_lock<std::mutex> lock (mutex);
	if (!stopped)
	{
		bool process (false);
		/* Random early delection levels
		Always process votes for test network (process = true)
		Stop processing with max 144 * 1024 votes */
		if (!node.network_params.network.is_test_network ())
		{
			// Level 0 (< 0.1%)
			if (votes.size () < 96 * 1024)
			{
				process = true;
			}
			// Level 1 (0.1-1%)
			else if (votes.size () < 112 * 1024)
			{
				process = (representatives_1.find (vote_a->account) != representatives_1.end ());
			}
			// Level 2 (1-5%)
			else if (votes.size () < 128 * 1024)
			{
				process = (representatives_2.find (vote_a->account) != representatives_2.end ());
			}
			// Level 3 (> 5%)
			else if (votes.size () < 144 * 1024)
			{
				process = (representatives_3.find (vote_a->account) != representatives_3.end ());
			}
		}
		else
		{
			// Process for test network
			process = true;
		}
		if (process)
		{
			votes.push_back (std::make_pair (vote_a, channel_a));

			lock.unlock ();
			condition.notify_all ();
			lock.lock ();
		}
		else
		{
			node.stats.inc (btcb::stat::type::vote, btcb::stat::detail::vote_overflow);
		}
	}
}

void btcb::vote_processor::verify_votes (std::deque<std::pair<std::shared_ptr<btcb::vote>, std::shared_ptr<btcb::transport::channel>>> & votes_a)
{
	auto size (votes_a.size ());
	std::vector<unsigned char const *> messages;
	messages.reserve (size);
	std::vector<btcb::uint256_union> hashes;
	hashes.reserve (size);
	std::vector<size_t> lengths (size, sizeof (btcb::uint256_union));
	std::vector<unsigned char const *> pub_keys;
	pub_keys.reserve (size);
	std::vector<unsigned char const *> signatures;
	signatures.reserve (size);
	std::vector<int> verifications;
	verifications.resize (size);
	for (auto & vote : votes_a)
	{
		hashes.push_back (vote.first->hash ());
		messages.push_back (hashes.back ().bytes.data ());
		pub_keys.push_back (vote.first->account.bytes.data ());
		signatures.push_back (vote.first->signature.bytes.data ());
	}
	btcb::signature_check_set check = { size, messages.data (), lengths.data (), pub_keys.data (), signatures.data (), verifications.data () };
	node.checker.verify (check);
	std::remove_reference_t<decltype (votes_a)> result;
	auto i (0);
	for (auto & vote : votes_a)
	{
		assert (verifications[i] == 1 || verifications[i] == 0);
		if (verifications[i] == 1)
		{
			result.push_back (vote);
		}
		++i;
	}
	votes_a.swap (result);
}

// node.active.mutex lock required
btcb::vote_code btcb::vote_processor::vote_blocking (btcb::transaction const & transaction_a, std::shared_ptr<btcb::vote> vote_a, std::shared_ptr<btcb::transport::channel> channel_a, bool validated)
{
	assert (!node.active.mutex.try_lock ());
	auto result (btcb::vote_code::invalid);
	if (validated || !vote_a->validate ())
	{
		auto max_vote (node.store.vote_max (transaction_a, vote_a));
		result = btcb::vote_code::replay;
		if (!node.active.vote (vote_a, true))
		{
			result = btcb::vote_code::vote;
		}
		switch (result)
		{
			case btcb::vote_code::vote:
				node.observers.vote.notify (transaction_a, vote_a, channel_a);
			case btcb::vote_code::replay:
				// This tries to assist rep nodes that have lost track of their highest sequence number by replaying our highest known vote back to them
				// Only do this if the sequence number is significantly different to account for network reordering
				// Amplify attack considerations: We're sending out a confirm_ack in response to a confirm_ack for no net traffic increase
				if (max_vote->sequence > vote_a->sequence + 10000)
				{
					btcb::confirm_ack confirm (max_vote);
					channel_a->send_buffer (confirm.to_bytes (), btcb::stat::detail::confirm_ack);
				}
				break;
			case btcb::vote_code::invalid:
				assert (false);
				break;
		}
	}
	std::string status;
	switch (result)
	{
		case btcb::vote_code::invalid:
			status = "Invalid";
			node.stats.inc (btcb::stat::type::vote, btcb::stat::detail::vote_invalid);
			break;
		case btcb::vote_code::replay:
			status = "Replay";
			node.stats.inc (btcb::stat::type::vote, btcb::stat::detail::vote_replay);
			break;
		case btcb::vote_code::vote:
			status = "Vote";
			node.stats.inc (btcb::stat::type::vote, btcb::stat::detail::vote_valid);
			break;
	}
	if (node.config.logging.vote_logging ())
	{
		node.logger.try_log (boost::str (boost::format ("Vote from: %1% sequence: %2% block(s): %3%status: %4%") % vote_a->account.to_account () % std::to_string (vote_a->sequence) % vote_a->hashes_string () % status));
	}
	return result;
}

void btcb::vote_processor::stop ()
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void btcb::vote_processor::flush ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (active || !votes.empty ())
	{
		condition.wait (lock);
	}
}

void btcb::vote_processor::calculate_weights ()
{
	std::unique_lock<std::mutex> lock (mutex);
	if (!stopped)
	{
		representatives_1.clear ();
		representatives_2.clear ();
		representatives_3.clear ();
		auto supply (node.online_reps.online_stake ());
		auto transaction (node.store.tx_begin_read ());
		for (auto i (node.store.representation_begin (transaction)), n (node.store.representation_end ()); i != n; ++i)
		{
			btcb::account representative (i->first);
			auto weight (node.ledger.weight (transaction, representative));
			if (weight > supply / 1000) // 0.1% or above (level 1)
			{
				representatives_1.insert (representative);
				if (weight > supply / 100) // 1% or above (level 2)
				{
					representatives_2.insert (representative);
					if (weight > supply / 20) // 5% or above (level 3)
					{
						representatives_3.insert (representative);
					}
				}
			}
		}
	}
}

namespace btcb
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_processor & vote_processor, const std::string & name)
{
	size_t votes_count = 0;
	size_t representatives_1_count = 0;
	size_t representatives_2_count = 0;
	size_t representatives_3_count = 0;

	{
		std::lock_guard<std::mutex> (vote_processor.mutex);
		votes_count = vote_processor.votes.size ();
		representatives_1_count = vote_processor.representatives_1.size ();
		representatives_2_count = vote_processor.representatives_2.size ();
		representatives_3_count = vote_processor.representatives_3.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "votes", votes_count, sizeof (decltype (vote_processor.votes)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "representatives_1", representatives_1_count, sizeof (decltype (vote_processor.representatives_1)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "representatives_2", representatives_2_count, sizeof (decltype (vote_processor.representatives_2)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "representatives_3", representatives_3_count, sizeof (decltype (vote_processor.representatives_3)::value_type) }));
	return composite;
}

std::unique_ptr<seq_con_info_component> collect_seq_con_info (rep_crawler & rep_crawler, const std::string & name)
{
	size_t count = 0;
	{
		std::lock_guard<std::mutex> guard (rep_crawler.active_mutex);
		count = rep_crawler.active.size ();
	}

	auto sizeof_element = sizeof (decltype (rep_crawler.active)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "active", count, sizeof_element }));
	return composite;
}

std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_processor & block_processor, const std::string & name)
{
	size_t state_blocks_count = 0;
	size_t blocks_count = 0;
	size_t blocks_hashes_count = 0;
	size_t forced_count = 0;
	size_t rolled_back_count = 0;

	{
		std::lock_guard<std::mutex> guard (block_processor.mutex);
		state_blocks_count = block_processor.state_blocks.size ();
		blocks_count = block_processor.blocks.size ();
		blocks_hashes_count = block_processor.blocks_hashes.size ();
		forced_count = block_processor.forced.size ();
		rolled_back_count = block_processor.rolled_back.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "state_blocks", state_blocks_count, sizeof (decltype (block_processor.state_blocks)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks", blocks_count, sizeof (decltype (block_processor.blocks)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks_hashes", blocks_hashes_count, sizeof (decltype (block_processor.blocks_hashes)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "forced", forced_count, sizeof (decltype (block_processor.forced)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "rolled_back", rolled_back_count, sizeof (decltype (block_processor.rolled_back)::value_type) }));
	composite->add_component (collect_seq_con_info (block_processor.generator, "generator"));
	return composite;
}
}

btcb::node::node (btcb::node_init & init_a, boost::asio::io_context & io_ctx_a, uint16_t peering_port_a, boost::filesystem::path const & application_path_a, btcb::alarm & alarm_a, btcb::logging const & logging_a, btcb::work_pool & work_a) :
node (init_a, io_ctx_a, application_path_a, alarm_a, btcb::node_config (peering_port_a, logging_a), work_a)
{
}

btcb::node::node (btcb::node_init & init_a, boost::asio::io_context & io_ctx_a, boost::filesystem::path const & application_path_a, btcb::alarm & alarm_a, btcb::node_config const & config_a, btcb::work_pool & work_a, btcb::node_flags flags_a, bool delay_frontier_confirmation_height_updating) :
io_ctx (io_ctx_a),
config (config_a),
flags (flags_a),
alarm (alarm_a),
work (work_a),
logger (config_a.logging.min_time_between_log_output),
store_impl (std::make_unique<btcb::mdb_store> (init_a.block_store_init, logger, application_path_a / "data.ldb", config_a.diagnostics_config.txn_tracking, config_a.block_processor_batch_max_time, config_a.lmdb_max_dbs, !flags.disable_unchecked_drop, flags.sideband_batch_size)),
store (*store_impl),
wallets_store_impl (std::make_unique<btcb::mdb_wallets_store> (init_a.wallets_store_init, application_path_a / "wallets.ldb", config_a.lmdb_max_dbs)),
wallets_store (*wallets_store_impl),
gap_cache (*this),
ledger (store, stats, config.epoch_block_link, config.epoch_block_signer),
checker (config.signature_checker_threads),
network (*this, config.peering_port),
bootstrap_initiator (*this),
bootstrap (config.peering_port, *this),
application_path (application_path_a),
port_mapping (*this),
vote_processor (*this),
rep_crawler (*this),
warmed_up (0),
block_processor (*this),
block_processor_thread ([this]() {
	btcb::thread_role::set (btcb::thread_role::name::block_processing);
	this->block_processor.process_blocks ();
}),
online_reps (*this, config.online_weight_minimum.number ()),
wallets (init_a.wallets_store_init, *this),
stats (config.stat_config),
vote_uniquer (block_uniquer),
active (*this, delay_frontier_confirmation_height_updating),
confirmation_height_processor (pending_confirmation_height, store, ledger.stats, active, ledger.epoch_link, logger),
payment_observer_processor (observers.blocks),
startup_time (std::chrono::steady_clock::now ())
{
	if (!init_a.error ())
	{
		if (config.websocket_config.enabled)
		{
			auto endpoint_l (btcb::tcp_endpoint (config.websocket_config.address, config.websocket_config.port));
			websocket_server = std::make_shared<btcb::websocket::listener> (*this, endpoint_l);
			this->websocket_server->run ();
		}

		wallets.observer = [this](bool active) {
			observers.wallet.notify (active);
		};
		network.channel_observer = [this](std::shared_ptr<btcb::transport::channel> channel_a) {
			observers.endpoint.notify (channel_a);
		};
		network.disconnect_observer = [this]() {
			observers.disconnect.notify ();
		};
		if (!config.callback_address.empty ())
		{
			observers.blocks.add ([this](std::shared_ptr<btcb::block> block_a, btcb::account const & account_a, btcb::amount const & amount_a, bool is_state_send_a) {
				if (this->block_arrival.recent (block_a->hash ()))
				{
					auto node_l (shared_from_this ());
					background ([node_l, block_a, account_a, amount_a, is_state_send_a]() {
						boost::property_tree::ptree event;
						event.add ("account", account_a.to_account ());
						event.add ("hash", block_a->hash ().to_string ());
						std::string block_text;
						block_a->serialize_json (block_text);
						event.add ("block", block_text);
						event.add ("amount", amount_a.to_string_dec ());
						if (is_state_send_a)
						{
							event.add ("is_send", is_state_send_a);
							event.add ("subtype", "send");
						}
						// Subtype field
						else if (block_a->type () == btcb::block_type::state)
						{
							if (block_a->link ().is_zero ())
							{
								event.add ("subtype", "change");
							}
							else if (amount_a == 0 && !node_l->ledger.epoch_link.is_zero () && node_l->ledger.is_epoch_link (block_a->link ()))
							{
								event.add ("subtype", "epoch");
							}
							else
							{
								event.add ("subtype", "receive");
							}
						}
						std::stringstream ostream;
						boost::property_tree::write_json (ostream, event);
						ostream.flush ();
						auto body (std::make_shared<std::string> (ostream.str ()));
						auto address (node_l->config.callback_address);
						auto port (node_l->config.callback_port);
						auto target (std::make_shared<std::string> (node_l->config.callback_target));
						auto resolver (std::make_shared<boost::asio::ip::tcp::resolver> (node_l->io_ctx));
						resolver->async_resolve (boost::asio::ip::tcp::resolver::query (address, std::to_string (port)), [node_l, address, port, target, body, resolver](boost::system::error_code const & ec, boost::asio::ip::tcp::resolver::iterator i_a) {
							if (!ec)
							{
								node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
							}
							else
							{
								if (node_l->config.logging.callback_logging ())
								{
									node_l->logger.always_log (boost::str (boost::format ("Error resolving callback: %1%:%2%: %3%") % address % port % ec.message ()));
								}
								node_l->stats.inc (btcb::stat::type::error, btcb::stat::detail::http_callback, btcb::stat::dir::out);
							}
						});
					});
				}
			});
		}
		if (websocket_server)
		{
			observers.blocks.add ([this](std::shared_ptr<btcb::block> block_a, btcb::account const & account_a, btcb::amount const & amount_a, bool is_state_send_a) {
				if (this->websocket_server->any_subscribers (btcb::websocket::topic::confirmation))
				{
					if (this->block_arrival.recent (block_a->hash ()))
					{
						std::string subtype;
						if (is_state_send_a)
						{
							subtype = "send";
						}
						else if (block_a->type () == btcb::block_type::state)
						{
							if (block_a->link ().is_zero ())
							{
								subtype = "change";
							}
							else if (amount_a == 0 && !this->ledger.epoch_link.is_zero () && this->ledger.is_epoch_link (block_a->link ()))
							{
								subtype = "epoch";
							}
							else
							{
								subtype = "receive";
							}
						}
						btcb::websocket::message_builder builder;
						auto msg (builder.block_confirmed (block_a, account_a, amount_a, subtype));
						this->websocket_server->broadcast (msg);
					}
				}
			});
		}
		observers.endpoint.add ([this](std::shared_ptr<btcb::transport::channel> channel_a) {
			if (channel_a->get_type () == btcb::transport::transport_type::udp)
			{
				this->network.send_keepalive (channel_a);
			}
			else
			{
				this->network.send_keepalive_self (channel_a);
			}
		});
		observers.vote.add ([this](btcb::transaction const & transaction, std::shared_ptr<btcb::vote> vote_a, std::shared_ptr<btcb::transport::channel> channel_a) {
			this->gap_cache.vote (vote_a);
			this->online_reps.observe (vote_a->account);
			btcb::uint128_t rep_weight;
			btcb::uint128_t min_rep_weight;
			{
				rep_weight = ledger.weight (transaction, vote_a->account);
				min_rep_weight = online_reps.online_stake () / 1000;
			}
			if (rep_weight > min_rep_weight)
			{
				bool rep_crawler_exists (false);
				for (auto hash : *vote_a)
				{
					if (this->rep_crawler.exists (hash))
					{
						rep_crawler_exists = true;
						break;
					}
				}
				if (rep_crawler_exists)
				{
					// We see a valid non-replay vote for a block we requested, this node is probably a representative
					if (this->rep_crawler.response (channel_a, vote_a->account, rep_weight))
					{
						logger.try_log (boost::str (boost::format ("Found a representative at %1%") % channel_a->to_string ()));
						// Rebroadcasting all active votes to new representative
						auto blocks (this->active.list_blocks (true));
						for (auto i (blocks.begin ()), n (blocks.end ()); i != n; ++i)
						{
							if (*i != nullptr)
							{
								btcb::confirm_req req (*i);
								channel_a->send (req);
							}
						}
					}
				}
			}
		});
		if (this->websocket_server)
		{
			observers.vote.add ([this](btcb::transaction const & transaction, std::shared_ptr<btcb::vote> vote_a, std::shared_ptr<btcb::transport::channel> channel_a) {
				if (this->websocket_server->any_subscribers (btcb::websocket::topic::vote))
				{
					btcb::websocket::message_builder builder;
					auto msg (builder.vote_received (vote_a));
					this->websocket_server->broadcast (msg);
				}
			});
		}
		if (BTCB_VERSION_PATCH == 0)
		{
			logger.always_log ("Node starting, version: ", BTCB_MAJOR_MINOR_VERSION);
		}
		else
		{
			logger.always_log ("Node starting, version: ", BTCB_MAJOR_MINOR_RC_VERSION);
		}

		logger.always_log (boost::str (boost::format ("Work pool running %1% threads") % work.threads.size ()));

		if (config.logging.node_lifetime_tracing ())
		{
			logger.always_log ("Constructing node");
		}

		// First do a pass with a read to see if any writing needs doing, this saves needing to open a write lock (and potentially blocking)
		auto is_initialized (false);
		{
			auto transaction (store.tx_begin_read ());
			is_initialized = (store.latest_begin (transaction) != store.latest_end ());
		}

		btcb::genesis genesis;
		if (!is_initialized)
		{
			auto transaction (store.tx_begin_write ());
			// Store was empty meaning we just created it, add the genesis block
			store.initialize (transaction, genesis);
		}

		auto transaction (store.tx_begin_read ());
		if (!store.block_exists (transaction, genesis.hash ()))
		{
			logger.always_log ("Genesis block not found. Make sure the node network ID is correct.");
			std::exit (1);
		}

		node_id = btcb::keypair ();
		logger.always_log ("Node ID: ", node_id.pub.to_account ());

		const uint8_t * weight_buffer = network_params.network.is_live_network () ? btcb_bootstrap_weights_live : btcb_bootstrap_weights_beta;
		size_t weight_size = network_params.network.is_live_network () ? btcb_bootstrap_weights_live_size : btcb_bootstrap_weights_beta_size;
		if (false && (network_params.network.is_live_network () || network_params.network.is_beta_network ()))
		{
			btcb::bufferstream weight_stream ((const uint8_t *)weight_buffer, weight_size);
			btcb::uint128_union block_height;
			if (!btcb::try_read (weight_stream, block_height))
			{
				auto max_blocks = (uint64_t)block_height.number ();
				auto transaction (store.tx_begin_read ());
				if (ledger.store.block_count (transaction).sum () < max_blocks)
				{
					ledger.bootstrap_weight_max_blocks = max_blocks;
					while (true)
					{
						btcb::account account;
						if (btcb::try_read (weight_stream, account.bytes))
						{
							break;
						}
						btcb::amount weight;
						if (btcb::try_read (weight_stream, weight.bytes))
						{
							break;
						}
						logger.always_log ("Using bootstrap rep weight: ", account.to_account (), " -> ", weight.format_balance (Mbcb_ratio, 0, true), " BCB");
						ledger.bootstrap_weights[account] = weight.number ();
					}
				}
			}
		}
	}
}

btcb::node::~node ()
{
	if (config.logging.node_lifetime_tracing ())
	{
		logger.always_log ("Destructing node");
	}
	stop ();
}

void btcb::node::do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const & address, uint16_t port, std::shared_ptr<std::string> target, std::shared_ptr<std::string> body, std::shared_ptr<boost::asio::ip::tcp::resolver> resolver)
{
	if (i_a != boost::asio::ip::tcp::resolver::iterator{})
	{
		auto node_l (shared_from_this ());
		auto sock (std::make_shared<boost::asio::ip::tcp::socket> (node_l->io_ctx));
		sock->async_connect (i_a->endpoint (), [node_l, target, body, sock, address, port, i_a, resolver](boost::system::error_code const & ec) mutable {
			if (!ec)
			{
				auto req (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
				req->method (boost::beast::http::verb::post);
				req->target (*target);
				req->version (11);
				req->insert (boost::beast::http::field::host, address);
				req->insert (boost::beast::http::field::content_type, "application/json");
				req->body () = *body;
				req->prepare_payload ();
				boost::beast::http::async_write (*sock, *req, [node_l, sock, address, port, req, i_a, target, body, resolver](boost::system::error_code const & ec, size_t bytes_transferred) mutable {
					if (!ec)
					{
						auto sb (std::make_shared<boost::beast::flat_buffer> ());
						auto resp (std::make_shared<boost::beast::http::response<boost::beast::http::string_body>> ());
						boost::beast::http::async_read (*sock, *sb, *resp, [node_l, sb, resp, sock, address, port, i_a, target, body, resolver](boost::system::error_code const & ec, size_t bytes_transferred) mutable {
							if (!ec)
							{
								if (resp->result () == boost::beast::http::status::ok)
								{
									node_l->stats.inc (btcb::stat::type::http_callback, btcb::stat::detail::initiate, btcb::stat::dir::out);
								}
								else
								{
									if (node_l->config.logging.callback_logging ())
									{
										node_l->logger.try_log (boost::str (boost::format ("Callback to %1%:%2% failed with status: %3%") % address % port % resp->result ()));
									}
									node_l->stats.inc (btcb::stat::type::error, btcb::stat::detail::http_callback, btcb::stat::dir::out);
								}
							}
							else
							{
								if (node_l->config.logging.callback_logging ())
								{
									node_l->logger.try_log (boost::str (boost::format ("Unable complete callback: %1%:%2%: %3%") % address % port % ec.message ()));
								}
								node_l->stats.inc (btcb::stat::type::error, btcb::stat::detail::http_callback, btcb::stat::dir::out);
							};
						});
					}
					else
					{
						if (node_l->config.logging.callback_logging ())
						{
							node_l->logger.try_log (boost::str (boost::format ("Unable to send callback: %1%:%2%: %3%") % address % port % ec.message ()));
						}
						node_l->stats.inc (btcb::stat::type::error, btcb::stat::detail::http_callback, btcb::stat::dir::out);
					}
				});
			}
			else
			{
				if (node_l->config.logging.callback_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Unable to connect to callback address: %1%:%2%: %3%") % address % port % ec.message ()));
				}
				node_l->stats.inc (btcb::stat::type::error, btcb::stat::detail::http_callback, btcb::stat::dir::out);
				++i_a;
				node_l->do_rpc_callback (i_a, address, port, target, body, resolver);
			}
		});
	}
}

bool btcb::node::copy_with_compaction (boost::filesystem::path const & destination_file)
{
	return !mdb_env_copy2 (boost::polymorphic_downcast<btcb::mdb_store *> (store_impl.get ())->env.environment, destination_file.string ().c_str (), MDB_CP_COMPACT);
}

void btcb::node::process_fork (btcb::transaction const & transaction_a, std::shared_ptr<btcb::block> block_a)
{
	auto root (block_a->root ());
	if (!store.block_exists (transaction_a, block_a->type (), block_a->hash ()) && store.root_exists (transaction_a, block_a->root ()))
	{
		std::shared_ptr<btcb::block> ledger_block (ledger.forked_block (transaction_a, *block_a));
		if (ledger_block && !block_confirmed_or_being_confirmed (transaction_a, ledger_block->hash ()))
		{
			std::weak_ptr<btcb::node> this_w (shared_from_this ());
			if (!active.start (ledger_block, [this_w, root](std::shared_ptr<btcb::block>) {
				    if (auto this_l = this_w.lock ())
				    {
					    auto attempt (this_l->bootstrap_initiator.current_attempt ());
					    if (attempt && attempt->mode == btcb::bootstrap_mode::legacy)
					    {
						    auto transaction (this_l->store.tx_begin_read ());
						    auto account (this_l->ledger.store.frontier_get (transaction, root));
						    if (!account.is_zero ())
						    {
							    attempt->requeue_pull (btcb::pull_info (account, root, root));
						    }
						    else if (this_l->ledger.store.account_exists (transaction, root))
						    {
							    attempt->requeue_pull (btcb::pull_info (root, btcb::block_hash (0), btcb::block_hash (0)));
						    }
					    }
				    }
			    }))
			{
				logger.always_log (boost::str (boost::format ("Resolving fork between our block: %1% and block %2% both with root %3%") % ledger_block->hash ().to_string () % block_a->hash ().to_string () % block_a->root ().to_string ()));
				network.broadcast_confirm_req (ledger_block);
			}
		}
	}
}

namespace btcb
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (node & node, const std::string & name)
{
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (collect_seq_con_info (node.alarm, "alarm"));
	composite->add_component (collect_seq_con_info (node.work, "work"));
	composite->add_component (collect_seq_con_info (node.gap_cache, "gap_cache"));
	composite->add_component (collect_seq_con_info (node.ledger, "ledger"));
	composite->add_component (collect_seq_con_info (node.active, "active"));
	composite->add_component (collect_seq_con_info (node.bootstrap_initiator, "bootstrap_initiator"));
	composite->add_component (collect_seq_con_info (node.bootstrap, "bootstrap"));
	composite->add_component (node.network.tcp_channels.collect_seq_con_info ("tcp_channels"));
	composite->add_component (node.network.udp_channels.collect_seq_con_info ("udp_channels"));
	composite->add_component (collect_seq_con_info (node.observers, "observers"));
	composite->add_component (collect_seq_con_info (node.wallets, "wallets"));
	composite->add_component (collect_seq_con_info (node.vote_processor, "vote_processor"));
	composite->add_component (collect_seq_con_info (node.rep_crawler, "rep_crawler"));
	composite->add_component (collect_seq_con_info (node.block_processor, "block_processor"));
	composite->add_component (collect_seq_con_info (node.block_arrival, "block_arrival"));
	composite->add_component (collect_seq_con_info (node.online_reps, "online_reps"));
	composite->add_component (collect_seq_con_info (node.votes_cache, "votes_cache"));
	composite->add_component (collect_seq_con_info (node.block_uniquer, "block_uniquer"));
	composite->add_component (collect_seq_con_info (node.vote_uniquer, "vote_uniquer"));
	composite->add_component (collect_seq_con_info (node.confirmation_height_processor, "confirmation_height_processor"));
	composite->add_component (collect_seq_con_info (node.pending_confirmation_height, "pending_confirmation_height"));
	return composite;
}
}

btcb::gap_cache::gap_cache (btcb::node & node_a) :
node (node_a)
{
}

void btcb::gap_cache::add (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a, std::chrono::steady_clock::time_point time_point_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (blocks.get<1> ().find (hash_a));
	if (existing != blocks.get<1> ().end ())
	{
		blocks.get<1> ().modify (existing, [time_point_a](btcb::gap_information & info) {
			info.arrival = time_point_a;
		});
	}
	else
	{
		blocks.insert ({ time_point_a, hash_a, std::unordered_set<btcb::account> () });
		if (blocks.size () > max)
		{
			blocks.get<0> ().erase (blocks.get<0> ().begin ());
		}
	}
}

void btcb::gap_cache::vote (std::shared_ptr<btcb::vote> vote_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto transaction (node.store.tx_begin_read ());
	for (auto hash : *vote_a)
	{
		auto existing (blocks.get<1> ().find (hash));
		if (existing != blocks.get<1> ().end ())
		{
			auto is_new (false);
			blocks.get<1> ().modify (existing, [&](btcb::gap_information & info) { is_new = info.voters.insert (vote_a->account).second; });
			if (is_new)
			{
				uint128_t tally;
				for (auto & voter : existing->voters)
				{
					tally += node.ledger.weight (transaction, voter);
				}
				bool start_bootstrap (false);
				if (!node.flags.disable_lazy_bootstrap)
				{
					if (tally >= node.config.online_weight_minimum.number ())
					{
						start_bootstrap = true;
					}
				}
				else if (!node.flags.disable_legacy_bootstrap && tally > bootstrap_threshold (transaction))
				{
					start_bootstrap = true;
				}
				if (start_bootstrap)
				{
					auto node_l (node.shared ());
					auto now (std::chrono::steady_clock::now ());
					node.alarm.add (node_l->network_params.network.is_test_network () ? now + std::chrono::milliseconds (5) : now + std::chrono::seconds (5), [node_l, hash]() {
						auto transaction (node_l->store.tx_begin_read ());
						if (!node_l->store.block_exists (transaction, hash))
						{
							if (!node_l->bootstrap_initiator.in_progress ())
							{
								node_l->logger.try_log (boost::str (boost::format ("Missing block %1% which has enough votes to warrant lazy bootstrapping it") % hash.to_string ()));
							}
							if (!node_l->flags.disable_lazy_bootstrap)
							{
								node_l->bootstrap_initiator.bootstrap_lazy (hash);
							}
							else if (!node_l->flags.disable_legacy_bootstrap)
							{
								node_l->bootstrap_initiator.bootstrap ();
							}
						}
					});
				}
			}
		}
	}
}

btcb::uint128_t btcb::gap_cache::bootstrap_threshold (btcb::transaction const & transaction_a)
{
	auto result ((node.online_reps.online_stake () / 256) * node.config.bootstrap_fraction_numerator);
	return result;
}

size_t btcb::gap_cache::size ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return blocks.size ();
}

namespace btcb
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (gap_cache & gap_cache, const std::string & name)
{
	auto count = gap_cache.size ();
	auto sizeof_element = sizeof (decltype (gap_cache.blocks)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks", count, sizeof_element }));
	return composite;
}
}

void btcb::node::process_active (std::shared_ptr<btcb::block> incoming)
{
	block_arrival.add (incoming->hash ());
	block_processor.add (incoming, btcb::seconds_since_epoch ());
}

btcb::process_return btcb::node::process (btcb::block const & block_a)
{
	auto transaction (store.tx_begin_write ());
	auto result (ledger.process (transaction, block_a));
	return result;
}

void btcb::node::start ()
{
	network.start ();
	add_initial_peers ();
	if (!flags.disable_legacy_bootstrap)
	{
		ongoing_bootstrap ();
	}
	else if (!flags.disable_unchecked_cleanup)
	{
		ongoing_unchecked_cleanup ();
	}
	ongoing_store_flush ();
	rep_crawler.start ();
	ongoing_rep_calculation ();
	ongoing_peer_store ();
	ongoing_online_weight_calculation_queue ();
	if (config.tcp_incoming_connections_max > 0)
	{
		bootstrap.start ();
	}
	if (!flags.disable_backup)
	{
		backup_wallet ();
	}
	search_pending ();
	if (!flags.disable_wallet_bootstrap)
	{
		// Delay to start wallet lazy bootstrap
		auto this_l (shared ());
		alarm.add (std::chrono::steady_clock::now () + std::chrono::minutes (1), [this_l]() {
			this_l->bootstrap_wallet ();
		});
	}
	if (config.external_address != boost::asio::ip::address_v6{} && config.external_port != 0)
	{
		port_mapping.start ();
	}
}

void btcb::node::stop ()
{
	logger.always_log ("Node stopping");
	block_processor.stop ();
	if (block_processor_thread.joinable ())
	{
		block_processor_thread.join ();
	}
	vote_processor.stop ();
	confirmation_height_processor.stop ();
	active.stop ();
	network.stop ();
	if (websocket_server)
	{
		websocket_server->stop ();
	}
	bootstrap_initiator.stop ();
	bootstrap.stop ();
	port_mapping.stop ();
	checker.stop ();
	wallets.stop ();
}

void btcb::node::keepalive_preconfigured (std::vector<std::string> const & peers_a)
{
	for (auto i (peers_a.begin ()), n (peers_a.end ()); i != n; ++i)
	{
		keepalive (*i, network_params.network.default_node_port);
	}
}

btcb::block_hash btcb::node::latest (btcb::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.latest (transaction, account_a);
}

btcb::uint128_t btcb::node::balance (btcb::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.account_balance (transaction, account_a);
}

std::shared_ptr<btcb::block> btcb::node::block (btcb::block_hash const & hash_a)
{
	auto transaction (store.tx_begin_read ());
	return store.block_get (transaction, hash_a);
}

std::pair<btcb::uint128_t, btcb::uint128_t> btcb::node::balance_pending (btcb::account const & account_a)
{
	std::pair<btcb::uint128_t, btcb::uint128_t> result;
	auto transaction (store.tx_begin_read ());
	result.first = ledger.account_balance (transaction, account_a);
	result.second = ledger.account_pending (transaction, account_a);
	return result;
}

btcb::uint128_t btcb::node::weight (btcb::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	return ledger.weight (transaction, account_a);
}

btcb::account btcb::node::representative (btcb::account const & account_a)
{
	auto transaction (store.tx_begin_read ());
	btcb::account_info info;
	btcb::account result (0);
	if (!store.account_get (transaction, account_a, info))
	{
		result = info.rep_block;
	}
	return result;
}

void btcb::node::ongoing_rep_calculation ()
{
	auto now (std::chrono::steady_clock::now ());
	vote_processor.calculate_weights ();
	std::weak_ptr<btcb::node> node_w (shared_from_this ());
	alarm.add (now + std::chrono::minutes (10), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_rep_calculation ();
		}
	});
}

void btcb::node::ongoing_bootstrap ()
{
	auto next_wakeup (300);
	if (warmed_up < 3)
	{
		// Re-attempt bootstrapping more aggressively on startup
		next_wakeup = 5;
		if (!bootstrap_initiator.in_progress () && !network.empty ())
		{
			++warmed_up;
		}
	}
	bootstrap_initiator.bootstrap ();
	std::weak_ptr<btcb::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (next_wakeup), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_bootstrap ();
		}
	});
}

void btcb::node::ongoing_store_flush ()
{
	{
		auto transaction (store.tx_begin_write ());
		store.flush (transaction);
	}
	std::weak_ptr<btcb::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_store_flush ();
		}
	});
}

void btcb::node::ongoing_peer_store ()
{
	bool stored (network.tcp_channels.store_all (true));
	network.udp_channels.store_all (!stored);
	std::weak_ptr<btcb::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.peer_interval, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_peer_store ();
		}
	});
}

void btcb::node::backup_wallet ()
{
	auto transaction (wallets.tx_begin_read ());
	for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n; ++i)
	{
		boost::system::error_code error_chmod;
		auto backup_path (application_path / "backup");

		boost::filesystem::create_directories (backup_path);
		btcb::set_secure_perm_directory (backup_path, error_chmod);
		i->second->store.write_backup (transaction, backup_path / (i->first.to_string () + ".json"));
	}
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.backup_interval, [this_l]() {
		this_l->backup_wallet ();
	});
}

void btcb::node::search_pending ()
{
	// Reload wallets from disk
	wallets.reload ();
	// Search pending
	wallets.search_pending_all ();
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.search_pending_interval, [this_l]() {
		this_l->search_pending ();
	});
}

void btcb::node::bootstrap_wallet ()
{
	std::deque<btcb::account> accounts;
	{
		std::lock_guard<std::mutex> lock (wallets.mutex);
		auto transaction (wallets.tx_begin_read ());
		for (auto i (wallets.items.begin ()), n (wallets.items.end ()); i != n && accounts.size () < 128; ++i)
		{
			auto & wallet (*i->second);
			std::lock_guard<std::recursive_mutex> wallet_lock (wallet.store.mutex);
			for (auto j (wallet.store.begin (transaction)), m (wallet.store.end ()); j != m && accounts.size () < 128; ++j)
			{
				btcb::account account (j->first);
				accounts.push_back (account);
			}
		}
	}
	bootstrap_initiator.bootstrap_wallet (accounts);
}

void btcb::node::unchecked_cleanup ()
{
	std::deque<btcb::unchecked_key> cleaning_list;
	// Collect old unchecked keys
	{
		auto now (btcb::seconds_since_epoch ());
		auto transaction (store.tx_begin_read ());
		// Max 128k records to clean, max 2 minutes reading to prevent slow i/o systems start issues
		for (auto i (store.unchecked_begin (transaction)), n (store.unchecked_end ()); i != n && cleaning_list.size () < 128 * 1024 && btcb::seconds_since_epoch () - now < 120; ++i)
		{
			btcb::unchecked_key key (i->first);
			btcb::unchecked_info info (i->second);
			if ((now - info.modified) > static_cast<uint64_t> (config.unchecked_cutoff_time.count ()))
			{
				cleaning_list.push_back (key);
			}
		}
	}
	// Delete old unchecked keys in batches
	while (!cleaning_list.empty ())
	{
		size_t deleted_count (0);
		auto transaction (store.tx_begin_write ());
		while (deleted_count++ < 2 * 1024 && !cleaning_list.empty ())
		{
			auto key (cleaning_list.front ());
			cleaning_list.pop_front ();
			store.unchecked_del (transaction, key);
		}
	}
}

void btcb::node::ongoing_unchecked_cleanup ()
{
	if (!bootstrap_initiator.in_progress ())
	{
		unchecked_cleanup ();
	}
	auto this_l (shared ());
	alarm.add (std::chrono::steady_clock::now () + network_params.node.unchecked_cleaning_interval, [this_l]() {
		this_l->ongoing_unchecked_cleanup ();
	});
}

int btcb::node::price (btcb::uint128_t const & balance_a, int amount_a)
{
	assert (balance_a >= amount_a * btcb::Gbcb_ratio);
	auto balance_l (balance_a);
	double result (0.0);
	for (auto i (0); i < amount_a; ++i)
	{
		balance_l -= btcb::Gbcb_ratio;
		auto balance_scaled ((balance_l / btcb::Mbcb_ratio).convert_to<double> ());
		auto units (balance_scaled / 1000.0);
		auto unit_price (((free_cutoff - units) / free_cutoff) * price_max);
		result += std::min (std::max (0.0, unit_price), price_max);
	}
	return static_cast<int> (result * 100.0);
}

namespace
{
class work_request
{
public:
	work_request (boost::asio::io_context & io_ctx_a, boost::asio::ip::address address_a, uint16_t port_a) :
	address (address_a),
	port (port_a),
	socket (io_ctx_a)
	{
	}
	boost::asio::ip::address address;
	uint16_t port;
	boost::beast::flat_buffer buffer;
	boost::beast::http::response<boost::beast::http::string_body> response;
	boost::asio::ip::tcp::socket socket;
};
class distributed_work : public std::enable_shared_from_this<distributed_work>
{
public:
	distributed_work (std::shared_ptr<btcb::node> const & node_a, btcb::block_hash const & root_a, std::function<void(uint64_t)> callback_a, uint64_t difficulty_a) :
	distributed_work (1, node_a, root_a, callback_a, difficulty_a)
	{
		assert (node_a != nullptr);
	}
	distributed_work (unsigned int backoff_a, std::shared_ptr<btcb::node> const & node_a, btcb::block_hash const & root_a, std::function<void(uint64_t)> callback_a, uint64_t difficulty_a) :
	callback (callback_a),
	backoff (backoff_a),
	node (node_a),
	root (root_a),
	need_resolve (node_a->config.work_peers),
	difficulty (difficulty_a)
	{
		assert (node_a != nullptr);
		completed.clear ();
	}
	void start ()
	{
		if (need_resolve.empty ())
		{
			start_work ();
		}
		else
		{
			auto current (need_resolve.back ());
			need_resolve.pop_back ();
			auto this_l (shared_from_this ());
			boost::system::error_code ec;
			auto parsed_address (boost::asio::ip::address_v6::from_string (current.first, ec));
			if (!ec)
			{
				outstanding[parsed_address] = current.second;
				start ();
			}
			else
			{
				node->network.resolver.async_resolve (boost::asio::ip::udp::resolver::query (current.first, std::to_string (current.second)), [current, this_l](boost::system::error_code const & ec, boost::asio::ip::udp::resolver::iterator i_a) {
					if (!ec)
					{
						for (auto i (i_a), n (boost::asio::ip::udp::resolver::iterator{}); i != n; ++i)
						{
							auto endpoint (i->endpoint ());
							this_l->outstanding[endpoint.address ()] = endpoint.port ();
						}
					}
					else
					{
						this_l->node->logger.try_log (boost::str (boost::format ("Error resolving work peer: %1%:%2%: %3%") % current.first % current.second % ec.message ()));
					}
					this_l->start ();
				});
			}
		}
	}
	void start_work ()
	{
		if (!outstanding.empty ())
		{
			auto this_l (shared_from_this ());
			std::lock_guard<std::mutex> lock (mutex);
			for (auto const & i : outstanding)
			{
				auto host (i.first);
				auto service (i.second);
				node->background ([this_l, host, service]() {
					auto connection (std::make_shared<work_request> (this_l->node->io_ctx, host, service));
					connection->socket.async_connect (btcb::tcp_endpoint (host, service), [this_l, connection](boost::system::error_code const & ec) {
						if (!ec)
						{
							std::string request_string;
							{
								boost::property_tree::ptree request;
								request.put ("action", "work_generate");
								request.put ("hash", this_l->root.to_string ());
								request.put ("difficulty", btcb::to_string_hex (this_l->difficulty));
								std::stringstream ostream;
								boost::property_tree::write_json (ostream, request);
								request_string = ostream.str ();
							}
							auto request (std::make_shared<boost::beast::http::request<boost::beast::http::string_body>> ());
							request->method (boost::beast::http::verb::post);
							request->target ("/");
							request->version (11);
							request->body () = request_string;
							request->prepare_payload ();
							boost::beast::http::async_write (connection->socket, *request, [this_l, connection, request](boost::system::error_code const & ec, size_t bytes_transferred) {
								if (!ec)
								{
									boost::beast::http::async_read (connection->socket, connection->buffer, connection->response, [this_l, connection](boost::system::error_code const & ec, size_t bytes_transferred) {
										if (!ec)
										{
											if (connection->response.result () == boost::beast::http::status::ok)
											{
												this_l->success (connection->response.body (), connection->address);
											}
											else
											{
												this_l->node->logger.try_log (boost::str (boost::format ("Work peer responded with an error %1% %2%: %3%") % connection->address % connection->port % connection->response.result ()));
												this_l->failure (connection->address);
											}
										}
										else
										{
											this_l->node->logger.try_log (boost::str (boost::format ("Unable to read from work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ()));
											this_l->failure (connection->address);
										}
									});
								}
								else
								{
									this_l->node->logger.try_log (boost::str (boost::format ("Unable to write to work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ()));
									this_l->failure (connection->address);
								}
							});
						}
						else
						{
							this_l->node->logger.try_log (boost::str (boost::format ("Unable to connect to work_peer %1% %2%: %3% (%4%)") % connection->address % connection->port % ec.message () % ec.value ()));
							this_l->failure (connection->address);
						}
					});
				});
			}
		}
		else
		{
			handle_failure (true);
		}
	}
	void stop ()
	{
		auto this_l (shared_from_this ());
		std::lock_guard<std::mutex> lock (mutex);
		for (auto const & i : outstanding)
		{
			auto host (i.first);
			node->background ([this_l, host]() {
				std::string request_string;
				{
					boost::property_tree::ptree request;
					request.put ("action", "work_cancel");
					request.put ("hash", this_l->root.to_string ());
					std::stringstream ostream;
					boost::property_tree::write_json (ostream, request);
					request_string = ostream.str ();
				}
				boost::beast::http::request<boost::beast::http::string_body> request;
				request.method (boost::beast::http::verb::post);
				request.target ("/");
				request.version (11);
				request.body () = request_string;
				request.prepare_payload ();
				auto socket (std::make_shared<boost::asio::ip::tcp::socket> (this_l->node->io_ctx));
				boost::beast::http::async_write (*socket, request, [socket](boost::system::error_code const & ec, size_t bytes_transferred) {
				});
			});
		}
		outstanding.clear ();
	}
	void success (std::string const & body_a, boost::asio::ip::address const & address)
	{
		auto last (remove (address));
		std::stringstream istream (body_a);
		try
		{
			boost::property_tree::ptree result;
			boost::property_tree::read_json (istream, result);
			auto work_text (result.get<std::string> ("work"));
			uint64_t work;
			if (!btcb::from_string_hex (work_text, work))
			{
				uint64_t result_difficulty (0);
				if (!btcb::work_validate (root, work, &result_difficulty) && result_difficulty >= difficulty)
				{
					set_once (work);
					stop ();
				}
				else
				{
					node->logger.try_log (boost::str (boost::format ("Incorrect work response from %1% for root %2% with diffuculty %3%: %4%") % address % root.to_string () % btcb::to_string_hex (difficulty) % work_text));
					handle_failure (last);
				}
			}
			else
			{
				node->logger.try_log (boost::str (boost::format ("Work response from %1% wasn't a number: %2%") % address % work_text));
				handle_failure (last);
			}
		}
		catch (...)
		{
			node->logger.try_log (boost::str (boost::format ("Work response from %1% wasn't parsable: %2%") % address % body_a));
			handle_failure (last);
		}
	}
	void set_once (uint64_t work_a)
	{
		if (!completed.test_and_set ())
		{
			callback (work_a);
		}
	}
	void failure (boost::asio::ip::address const & address)
	{
		auto last (remove (address));
		handle_failure (last);
	}
	void handle_failure (bool last)
	{
		if (last)
		{
			if (!completed.test_and_set ())
			{
				if (node->config.work_threads != 0 || node->work.opencl)
				{
					auto callback_l (callback);
					// clang-format off
					node->work.generate (root, [callback_l](boost::optional<uint64_t> const & work_a) {
						callback_l (work_a.value ());
					},
					difficulty);
					// clang-format on
				}
				else
				{
					if (backoff == 1 && node->config.logging.work_generation_time ())
					{
						node->logger.try_log ("Work peer(s) failed to generate work for root ", root.to_string (), ", retrying...");
					}
					auto now (std::chrono::steady_clock::now ());
					auto root_l (root);
					auto callback_l (callback);
					std::weak_ptr<btcb::node> node_w (node);
					auto next_backoff (std::min (backoff * 2, (unsigned int)60 * 5));
					// clang-format off
					node->alarm.add (now + std::chrono::seconds (backoff), [ node_w, root_l, callback_l, next_backoff, difficulty = difficulty ] {
						if (auto node_l = node_w.lock ())
						{
							auto work_generation (std::make_shared<distributed_work> (next_backoff, node_l, root_l, callback_l, difficulty));
							work_generation->start ();
						}
					});
					// clang-format on
				}
			}
		}
	}
	bool remove (boost::asio::ip::address const & address)
	{
		std::lock_guard<std::mutex> lock (mutex);
		outstanding.erase (address);
		return outstanding.empty ();
	}
	std::function<void(uint64_t)> callback;
	unsigned int backoff; // in seconds
	std::shared_ptr<btcb::node> node;
	btcb::block_hash root;
	std::mutex mutex;
	std::map<boost::asio::ip::address, uint16_t> outstanding;
	std::vector<std::pair<std::string, uint16_t>> need_resolve;
	std::atomic_flag completed;
	uint64_t difficulty;
};
}

void btcb::node::work_generate_blocking (btcb::block & block_a)
{
	work_generate_blocking (block_a, network_params.network.publish_threshold);
}

void btcb::node::work_generate_blocking (btcb::block & block_a, uint64_t difficulty_a)
{
	block_a.block_work_set (work_generate_blocking (block_a.root (), difficulty_a));
}

void btcb::node::work_generate (btcb::uint256_union const & hash_a, std::function<void(uint64_t)> callback_a)
{
	work_generate (hash_a, callback_a, network_params.network.publish_threshold);
}

void btcb::node::work_generate (btcb::uint256_union const & hash_a, std::function<void(uint64_t)> callback_a, uint64_t difficulty_a)
{
	auto work_generation (std::make_shared<distributed_work> (shared (), hash_a, callback_a, difficulty_a));
	work_generation->start ();
}

uint64_t btcb::node::work_generate_blocking (btcb::uint256_union const & block_a)
{
	return work_generate_blocking (block_a, network_params.network.publish_threshold);
}

uint64_t btcb::node::work_generate_blocking (btcb::uint256_union const & hash_a, uint64_t difficulty_a)
{
	std::promise<uint64_t> promise;
	std::future<uint64_t> future = promise.get_future ();
	// clang-format off
	work_generate (hash_a, [&promise](uint64_t work_a) {
		promise.set_value (work_a);
	},
	difficulty_a);
	// clang-format on
	return future.get ();
}

void btcb::node::add_initial_peers ()
{
	auto transaction (store.tx_begin_read ());
	for (auto i (store.peers_begin (transaction)), n (store.peers_end ()); i != n; ++i)
	{
		btcb::endpoint endpoint (boost::asio::ip::address_v6 (i->first.address_bytes ()), i->first.port ());
		if (!network.reachout (endpoint, config.allow_local_peers))
		{
			std::weak_ptr<btcb::node> node_w (shared_from_this ());
			network.tcp_channels.start_tcp (endpoint, [node_w](std::shared_ptr<btcb::transport::channel> channel_a) {
				if (auto node_l = node_w.lock ())
				{
					node_l->network.send_keepalive (channel_a);
					node_l->rep_crawler.query (channel_a);
				}
			});
		}
	}
}

void btcb::node::block_confirm (std::shared_ptr<btcb::block> block_a)
{
	active.start (block_a);
	network.broadcast_confirm_req (block_a);
	// Calculate votes for local representatives
	if (config.enable_voting && active.active (*block_a))
	{
		block_processor.generator.add (block_a->hash ());
	}
}

bool btcb::node::block_confirmed_or_being_confirmed (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a)
{
	return ledger.block_confirmed (transaction_a, hash_a) || confirmation_height_processor.is_processing_block (hash_a);
}

btcb::uint128_t btcb::node::delta () const
{
	auto result ((online_reps.online_stake () / 100) * config.online_weight_quorum);
	return result;
}

void btcb::node::ongoing_online_weight_calculation_queue ()
{
	std::weak_ptr<btcb::node> node_w (shared_from_this ());
	alarm.add (std::chrono::steady_clock::now () + (std::chrono::seconds (network_params.node.weight_period)), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->ongoing_online_weight_calculation ();
		}
	});
}

bool btcb::node::online () const
{
	return rep_crawler.total_weight () > (std::max (config.online_weight_minimum.number (), delta ()));
}

void btcb::node::ongoing_online_weight_calculation ()
{
	online_reps.sample ();
	ongoing_online_weight_calculation_queue ();
}

namespace
{
class confirmed_visitor : public btcb::block_visitor
{
public:
	confirmed_visitor (btcb::transaction const & transaction_a, btcb::node & node_a, std::shared_ptr<btcb::block> block_a, btcb::block_hash const & hash_a) :
	transaction (transaction_a),
	node (node_a),
	block (block_a),
	hash (hash_a)
	{
	}
	virtual ~confirmed_visitor () = default;
	void scan_receivable (btcb::account const & account_a)
	{
		for (auto i (node.wallets.items.begin ()), n (node.wallets.items.end ()); i != n; ++i)
		{
			auto wallet (i->second);
			auto transaction_l (node.wallets.tx_begin_read ());
			if (wallet->store.exists (transaction_l, account_a))
			{
				btcb::account representative;
				btcb::pending_info pending;
				representative = wallet->store.representative (transaction_l);
				auto error (node.store.pending_get (transaction, btcb::pending_key (account_a, hash), pending));
				if (!error)
				{
					auto node_l (node.shared ());
					auto amount (pending.amount.number ());
					wallet->receive_async (block, representative, amount, [](std::shared_ptr<btcb::block>) {});
				}
				else
				{
					if (!node.store.block_exists (transaction, hash))
					{
						node.logger.try_log (boost::str (boost::format ("Confirmed block is missing:  %1%") % hash.to_string ()));
						assert (false && "Confirmed block is missing");
					}
					else
					{
						node.logger.try_log (boost::str (boost::format ("Block %1% has already been received") % hash.to_string ()));
					}
				}
			}
		}
	}
	void state_block (btcb::state_block const & block_a) override
	{
		scan_receivable (block_a.hashables.link);
	}
	void send_block (btcb::send_block const & block_a) override
	{
		scan_receivable (block_a.hashables.destination);
	}
	void receive_block (btcb::receive_block const &) override
	{
	}
	void open_block (btcb::open_block const &) override
	{
	}
	void change_block (btcb::change_block const &) override
	{
	}
	btcb::transaction const & transaction;
	btcb::node & node;
	std::shared_ptr<btcb::block> block;
	btcb::block_hash const & hash;
};
}

void btcb::node::receive_confirmed (btcb::transaction const & transaction_a, std::shared_ptr<btcb::block> block_a, btcb::block_hash const & hash_a)
{
	confirmed_visitor visitor (transaction_a, *this, block_a, hash_a);
	block_a->visit (visitor);
}

void btcb::node::process_confirmed (std::shared_ptr<btcb::block> block_a, uint8_t iteration)
{
	auto hash (block_a->hash ());
	if (ledger.block_exists (block_a->type (), hash))
	{
		confirmation_height_processor.add (hash);

		auto transaction (store.tx_begin_read ());
		receive_confirmed (transaction, block_a, hash);
		auto account (ledger.account (transaction, hash));
		auto amount (ledger.amount (transaction, hash));
		bool is_state_send (false);
		btcb::account pending_account (0);
		if (auto state = dynamic_cast<btcb::state_block *> (block_a.get ()))
		{
			is_state_send = ledger.is_send (transaction, *state);
			pending_account = state->hashables.link;
		}
		if (auto send = dynamic_cast<btcb::send_block *> (block_a.get ()))
		{
			pending_account = send->hashables.destination;
		}
		observers.blocks.notify (block_a, account, amount, is_state_send);
		if (amount > 0)
		{
			observers.account_balance.notify (account, false);
			if (!pending_account.is_zero ())
			{
				observers.account_balance.notify (pending_account, true);
			}
		}
	}
	// Limit to 0.5 * 20 = 10 seconds (more than max block_processor::process_batch finish time)
	else if (iteration < 20)
	{
		iteration++;
		std::weak_ptr<btcb::node> node_w (shared ());
		alarm.add (std::chrono::steady_clock::now () + network_params.node.process_confirmed_interval, [node_w, block_a, iteration]() {
			if (auto node_l = node_w.lock ())
			{
				node_l->process_confirmed (block_a, iteration);
			}
		});
	}
}

void btcb::node::process_message (btcb::message const & message_a, std::shared_ptr<btcb::transport::channel> channel_a)
{
	network_message_visitor visitor (*this, channel_a);
	message_a.visit (visitor);
}

btcb::endpoint btcb::network::endpoint ()
{
	return udp_channels.get_local_endpoint ();
}

void btcb::network::cleanup (std::chrono::steady_clock::time_point const & cutoff_a)
{
	tcp_channels.purge (cutoff_a);
	udp_channels.purge (cutoff_a);
	if (node.network.empty ())
	{
		disconnect_observer ();
	}
}

void btcb::network::ongoing_cleanup ()
{
	cleanup (std::chrono::steady_clock::now () - node.network_params.node.cutoff);
	std::weak_ptr<btcb::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + node.network_params.node.period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.ongoing_cleanup ();
		}
	});
}

size_t btcb::network::size () const
{
	return tcp_channels.size () + udp_channels.size ();
}

size_t btcb::network::size_sqrt () const
{
	return (static_cast<size_t> (std::ceil (std::sqrt (size ()))));
}

bool btcb::network::empty () const
{
	return size () == 0;
}

bool btcb::block_arrival::add (btcb::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	auto inserted (arrival.insert (btcb::block_arrival_info{ now, hash_a }));
	auto result (!inserted.second);
	return result;
}

bool btcb::block_arrival::recent (btcb::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto now (std::chrono::steady_clock::now ());
	while (arrival.size () > arrival_size_min && arrival.begin ()->arrival + arrival_time_min < now)
	{
		arrival.erase (arrival.begin ());
	}
	return arrival.get<1> ().find (hash_a) != arrival.get<1> ().end ();
}

namespace btcb
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_arrival & block_arrival, const std::string & name)
{
	size_t count = 0;
	{
		std::lock_guard<std::mutex> guard (block_arrival.mutex);
		count = block_arrival.arrival.size ();
	}

	auto sizeof_element = sizeof (decltype (block_arrival.arrival)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "arrival", count, sizeof_element }));
	return composite;
}
}

btcb::online_reps::online_reps (btcb::node & node_a, btcb::uint128_t minimum_a) :
node (node_a),
minimum (minimum_a)
{
	auto transaction (node.ledger.store.tx_begin_read ());
	online = trend (transaction);
}

void btcb::online_reps::observe (btcb::account const & rep_a)
{
	auto transaction (node.ledger.store.tx_begin_read ());
	if (node.ledger.weight (transaction, rep_a) > 0)
	{
		std::lock_guard<std::mutex> lock (mutex);
		reps.insert (rep_a);
	}
}

void btcb::online_reps::sample ()
{
	auto transaction (node.ledger.store.tx_begin_write ());
	// Discard oldest entries
	while (node.ledger.store.online_weight_count (transaction) >= node.network_params.node.max_weight_samples)
	{
		auto oldest (node.ledger.store.online_weight_begin (transaction));
		assert (oldest != node.ledger.store.online_weight_end ());
		node.ledger.store.online_weight_del (transaction, oldest->first);
	}
	// Calculate current active rep weight
	btcb::uint128_t current;
	std::unordered_set<btcb::account> reps_copy;
	{
		std::lock_guard<std::mutex> lock (mutex);
		reps_copy.swap (reps);
	}
	for (auto & i : reps_copy)
	{
		current += node.ledger.weight (transaction, i);
	}
	node.ledger.store.online_weight_put (transaction, std::chrono::system_clock::now ().time_since_epoch ().count (), current);
	auto trend_l (trend (transaction));
	std::lock_guard<std::mutex> lock (mutex);
	online = trend_l;
}

btcb::uint128_t btcb::online_reps::trend (btcb::transaction & transaction_a)
{
	std::vector<btcb::uint128_t> items;
	items.reserve (node.network_params.node.max_weight_samples + 1);
	items.push_back (minimum);
	for (auto i (node.ledger.store.online_weight_begin (transaction_a)), n (node.ledger.store.online_weight_end ()); i != n; ++i)
	{
		items.push_back (i->second.number ());
	}

	// Pick median value for our target vote weight
	auto median_idx = items.size () / 2;
	nth_element (items.begin (), items.begin () + median_idx, items.end ());
	return btcb::uint128_t{ items[median_idx] };
}

btcb::uint128_t btcb::online_reps::online_stake () const
{
	std::lock_guard<std::mutex> lock (mutex);
	return std::max (online, minimum);
}

std::vector<btcb::account> btcb::online_reps::list ()
{
	std::vector<btcb::account> result;
	std::lock_guard<std::mutex> lock (mutex);
	for (auto & i : reps)
	{
		result.push_back (i);
	}
	return result;
}

namespace btcb
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (online_reps & online_reps, const std::string & name)
{
	size_t count = 0;
	{
		std::lock_guard<std::mutex> guard (online_reps.mutex);
		count = online_reps.reps.size ();
	}

	auto sizeof_element = sizeof (decltype (online_reps.reps)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "arrival", count, sizeof_element }));
	return composite;
}
}

std::shared_ptr<btcb::node> btcb::node::shared ()
{
	return shared_from_this ();
}

btcb::election_vote_result::election_vote_result (bool replay_a, bool processed_a)
{
	replay = replay_a;
	processed = processed_a;
}

btcb::election::election (btcb::node & node_a, std::shared_ptr<btcb::block> block_a, std::function<void(std::shared_ptr<btcb::block>)> const & confirmation_action_a) :
confirmation_action (confirmation_action_a),
node (node_a),
election_start (std::chrono::steady_clock::now ()),
status ({ block_a, 0 }),
confirmed (false),
stopped (false),
announcements (0)
{
	last_votes.insert (std::make_pair (node.network_params.random.not_an_account, btcb::vote_info{ std::chrono::steady_clock::now (), 0, block_a->hash () }));
	blocks.insert (std::make_pair (block_a->hash (), block_a));
	update_dependent ();
}

void btcb::election::compute_rep_votes (btcb::transaction const & transaction_a)
{
	if (node.config.enable_voting)
	{
		node.wallets.foreach_representative (transaction_a, [this, &transaction_a](btcb::public_key const & pub_a, btcb::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction_a, pub_a, prv_a, status.winner));
			this->node.vote_processor.vote (vote, std::make_shared<btcb::transport::channel_udp> (this->node.network.udp_channels, this->node.network.endpoint ()));
		});
	}
}

void btcb::election::confirm_once ()
{
	if (!confirmed.exchange (true))
	{
		status.election_end = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ());
		status.election_duration = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now () - election_start);
		auto winner_l (status.winner);
		auto node_l (node.shared ());
		auto confirmation_action_l (confirmation_action);
		node.background ([node_l, winner_l, confirmation_action_l]() {
			node_l->process_confirmed (winner_l);
			confirmation_action_l (winner_l);
		});
		if (announcements > node_l->active.announcement_long)
		{
			--node_l->active.long_unconfirmed_size;
		}
	}
}

void btcb::election::stop ()
{
	stopped = true;
}

bool btcb::election::have_quorum (btcb::tally_t const & tally_a, btcb::uint128_t tally_sum) const
{
	bool result = false;
	if (tally_sum >= node.config.online_weight_minimum.number ())
	{
		auto i (tally_a.begin ());
		auto first (i->first);
		++i;
		auto second (i != tally_a.end () ? i->first : 0);
		auto delta_l (node.delta ());
		result = tally_a.begin ()->first > (second + delta_l);
	}
	return result;
}

btcb::tally_t btcb::election::tally (btcb::transaction const & transaction_a)
{
	std::unordered_map<btcb::block_hash, btcb::uint128_t> block_weights;
	for (auto vote_info : last_votes)
	{
		block_weights[vote_info.second.hash] += node.ledger.weight (transaction_a, vote_info.first);
	}
	last_tally = block_weights;
	btcb::tally_t result;
	for (auto item : block_weights)
	{
		auto block (blocks.find (item.first));
		if (block != blocks.end ())
		{
			result.insert (std::make_pair (item.second, block->second));
		}
	}
	return result;
}

void btcb::election::confirm_if_quorum (btcb::transaction const & transaction_a)
{
	auto tally_l (tally (transaction_a));
	assert (!tally_l.empty ());
	auto winner (tally_l.begin ());
	auto block_l (winner->second);
	status.tally = winner->first;
	btcb::uint128_t sum (0);
	for (auto & i : tally_l)
	{
		sum += i.first;
	}
	if (sum >= node.config.online_weight_minimum.number () && block_l->hash () != status.winner->hash ())
	{
		auto node_l (node.shared ());
		node_l->block_processor.force (block_l);
		status.winner = block_l;
		update_dependent ();
		node_l->active.adjust_difficulty (block_l->hash ());
	}
	if (have_quorum (tally_l, sum))
	{
		if (node.config.logging.vote_logging () || blocks.size () > 1)
		{
			log_votes (tally_l);
		}
		confirm_once ();
	}
}

void btcb::election::log_votes (btcb::tally_t const & tally_a) const
{
	std::stringstream tally;
	tally << boost::str (boost::format ("\nVote tally for root %1%") % status.winner->root ().to_string ());
	for (auto i (tally_a.begin ()), n (tally_a.end ()); i != n; ++i)
	{
		tally << boost::str (boost::format ("\nBlock %1% weight %2%") % i->second->hash ().to_string () % i->first.convert_to<std::string> ());
	}
	for (auto i (last_votes.begin ()), n (last_votes.end ()); i != n; ++i)
	{
		tally << boost::str (boost::format ("\n%1% %2%") % i->first.to_account () % i->second.hash.to_string ());
	}
	node.logger.try_log (tally.str ());
}

btcb::election_vote_result btcb::election::vote (btcb::account rep, uint64_t sequence, btcb::block_hash block_hash)
{
	// see republish_vote documentation for an explanation of these rules
	auto transaction (node.store.tx_begin_read ());
	auto replay (false);
	auto supply (node.online_reps.online_stake ());
	auto weight (node.ledger.weight (transaction, rep));
	auto should_process (false);
	if (node.network_params.network.is_test_network () || weight > supply / 1000) // 0.1% or above
	{
		unsigned int cooldown;
		if (weight < supply / 100) // 0.1% to 1%
		{
			cooldown = 15;
		}
		else if (weight < supply / 20) // 1% to 5%
		{
			cooldown = 5;
		}
		else // 5% or above
		{
			cooldown = 1;
		}
		auto last_vote_it (last_votes.find (rep));
		if (last_vote_it == last_votes.end ())
		{
			should_process = true;
		}
		else
		{
			auto last_vote (last_vote_it->second);
			if (last_vote.sequence < sequence || (last_vote.sequence == sequence && last_vote.hash < block_hash))
			{
				if (last_vote.time <= std::chrono::steady_clock::now () - std::chrono::seconds (cooldown))
				{
					should_process = true;
				}
			}
			else
			{
				replay = true;
			}
		}
		if (should_process)
		{
			last_votes[rep] = { std::chrono::steady_clock::now (), sequence, block_hash };
			if (!confirmed)
			{
				confirm_if_quorum (transaction);
			}
		}
	}
	return btcb::election_vote_result (replay, should_process);
}

bool btcb::node::validate_block_by_previous (btcb::transaction const & transaction, std::shared_ptr<btcb::block> block_a)
{
	bool result (false);
	btcb::account account;
	if (!block_a->previous ().is_zero ())
	{
		if (store.block_exists (transaction, block_a->previous ()))
		{
			account = ledger.account (transaction, block_a->previous ());
		}
		else
		{
			result = true;
		}
	}
	else
	{
		account = block_a->root ();
	}
	if (!result && block_a->type () == btcb::block_type::state)
	{
		std::shared_ptr<btcb::state_block> block_l (std::static_pointer_cast<btcb::state_block> (block_a));
		btcb::amount prev_balance (0);
		if (!block_l->hashables.previous.is_zero ())
		{
			if (store.block_exists (transaction, block_l->hashables.previous))
			{
				prev_balance = ledger.balance (transaction, block_l->hashables.previous);
			}
			else
			{
				result = true;
			}
		}
		if (!result)
		{
			if (block_l->hashables.balance == prev_balance && !ledger.epoch_link.is_zero () && ledger.is_epoch_link (block_l->hashables.link))
			{
				account = ledger.epoch_signer;
			}
		}
	}
	if (!result && (account.is_zero () || btcb::validate_message (account, block_a->hash (), block_a->block_signature ())))
	{
		result = true;
	}
	return result;
}

bool btcb::election::publish (std::shared_ptr<btcb::block> block_a)
{
	auto result (false);
	if (blocks.size () >= 10)
	{
		if (last_tally[block_a->hash ()] < node.online_reps.online_stake () / 10)
		{
			result = true;
		}
	}
	if (!result)
	{
		auto transaction (node.store.tx_begin_read ());
		result = node.validate_block_by_previous (transaction, block_a);
		if (!result)
		{
			if (blocks.find (block_a->hash ()) == blocks.end ())
			{
				blocks.insert (std::make_pair (block_a->hash (), block_a));
				confirm_if_quorum (transaction);
				node.network.flood_block (block_a);
			}
		}
	}
	return result;
}

size_t btcb::election::last_votes_size ()
{
	std::lock_guard<std::mutex> lock (node.active.mutex);
	return last_votes.size ();
}

void btcb::election::update_dependent ()
{
	assert (!node.active.mutex.try_lock ());
	std::vector<btcb::block_hash> blocks_search;
	auto hash (status.winner->hash ());
	auto previous (status.winner->previous ());
	if (!previous.is_zero ())
	{
		blocks_search.push_back (previous);
	}
	auto source (status.winner->source ());
	if (!source.is_zero () && source != previous)
	{
		blocks_search.push_back (source);
	}
	auto link (status.winner->link ());
	if (!link.is_zero () && !node.ledger.is_epoch_link (link) && link != previous)
	{
		blocks_search.push_back (link);
	}
	for (auto & block_search : blocks_search)
	{
		auto existing (node.active.blocks.find (block_search));
		if (existing != node.active.blocks.end () && !existing->second->confirmed && !existing->second->stopped)
		{
			if (existing->second->dependent_blocks.find (hash) == existing->second->dependent_blocks.end ())
			{
				existing->second->dependent_blocks.insert (hash);
			}
		}
	}
}

int btcb::node::store_version ()
{
	auto transaction (store.tx_begin_read ());
	return store.version_get (transaction);
}

btcb::inactive_node::inactive_node (boost::filesystem::path const & path_a, uint16_t peering_port_a) :
path (path_a),
io_context (std::make_shared<boost::asio::io_context> ()),
alarm (*io_context),
work (1),
peering_port (peering_port_a)
{
	boost::system::error_code error_chmod;

	/*
	 * @warning May throw a filesystem exception
	 */
	boost::filesystem::create_directories (path);
	btcb::set_secure_perm_directory (path, error_chmod);
	logging.max_size = std::numeric_limits<std::uintmax_t>::max ();
	logging.init (path);
	node = std::make_shared<btcb::node> (init, *io_context, peering_port, path, alarm, logging, work);
	node->active.stop ();
}

btcb::inactive_node::~inactive_node ()
{
	node->stop ();
}

btcb::message_buffer_manager::message_buffer_manager (btcb::stat & stats_a, size_t size, size_t count) :
stats (stats_a),
free (count),
full (count),
slab (size * count),
entries (count),
stopped (false)
{
	assert (count > 0);
	assert (size > 0);
	auto slab_data (slab.data ());
	auto entry_data (entries.data ());
	for (auto i (0); i < count; ++i, ++entry_data)
	{
		*entry_data = { slab_data + i * size, 0, btcb::endpoint () };
		free.push_back (entry_data);
	}
}
btcb::message_buffer * btcb::message_buffer_manager::allocate ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && free.empty () && full.empty ())
	{
		stats.inc (btcb::stat::type::udp, btcb::stat::detail::blocking, btcb::stat::dir::in);
		condition.wait (lock);
	}
	btcb::message_buffer * result (nullptr);
	if (!free.empty ())
	{
		result = free.front ();
		free.pop_front ();
	}
	if (result == nullptr && !full.empty ())
	{
		result = full.front ();
		full.pop_front ();
		stats.inc (btcb::stat::type::udp, btcb::stat::detail::overflow, btcb::stat::dir::in);
	}
	release_assert (result || stopped);
	return result;
}
void btcb::message_buffer_manager::enqueue (btcb::message_buffer * data_a)
{
	assert (data_a != nullptr);
	{
		std::lock_guard<std::mutex> lock (mutex);
		full.push_back (data_a);
	}
	condition.notify_all ();
}
btcb::message_buffer * btcb::message_buffer_manager::dequeue ()
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!stopped && full.empty ())
	{
		condition.wait (lock);
	}
	btcb::message_buffer * result (nullptr);
	if (!full.empty ())
	{
		result = full.front ();
		full.pop_front ();
	}
	return result;
}
void btcb::message_buffer_manager::release (btcb::message_buffer * data_a)
{
	assert (data_a != nullptr);
	{
		std::lock_guard<std::mutex> lock (mutex);
		free.push_back (data_a);
	}
	condition.notify_all ();
}
void btcb::message_buffer_manager::stop ()
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		stopped = true;
	}
	condition.notify_all ();
}
