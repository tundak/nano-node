#include <btcb/node/node.hpp>
#include <btcb/node/transport/tcp.hpp>

btcb::transport::channel_tcp::channel_tcp (btcb::node & node_a, std::shared_ptr<btcb::socket> socket_a) :
channel (node_a),
socket (socket_a)
{
}

btcb::transport::channel_tcp::~channel_tcp ()
{
	std::lock_guard<std::mutex> lk (channel_mutex);
	if (socket)
	{
		socket->close ();
	}
}

size_t btcb::transport::channel_tcp::hash_code () const
{
	std::hash<::btcb::tcp_endpoint> hash;
	return hash (socket->remote_endpoint ());
}

bool btcb::transport::channel_tcp::operator== (btcb::transport::channel const & other_a) const
{
	bool result (false);
	auto other_l (dynamic_cast<btcb::transport::channel_tcp const *> (&other_a));
	if (other_l != nullptr)
	{
		return *this == *other_l;
	}
	return result;
}

void btcb::transport::channel_tcp::send_buffer (std::shared_ptr<std::vector<uint8_t>> buffer_a, btcb::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a)
{
	socket->async_write (buffer_a, tcp_callback (buffer_a, detail_a, socket->remote_endpoint (), callback_a));
}

std::function<void(boost::system::error_code const &, size_t)> btcb::transport::channel_tcp::callback (std::shared_ptr<std::vector<uint8_t>> buffer_a, btcb::stat::detail detail_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	return callback_a;
}

std::function<void(boost::system::error_code const &, size_t)> btcb::transport::channel_tcp::tcp_callback (std::shared_ptr<std::vector<uint8_t>> buffer_a, btcb::stat::detail detail_a, btcb::tcp_endpoint const & endpoint_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a) const
{
	// clang-format off
	return [ buffer_a, endpoint_a, node = std::weak_ptr<btcb::node> (node.shared ()), callback_a ](boost::system::error_code const & ec, size_t size_a)
	{
		if (auto node_l = node.lock ())
		{
			if (!ec)
			{
				node_l->network.tcp_channels.update (endpoint_a);
			}
			if (ec == boost::system::errc::host_unreachable)
			{
				node_l->stats.inc (btcb::stat::type::error, btcb::stat::detail::unreachable_host, btcb::stat::dir::out);
			}
			if (callback_a)
			{
				callback_a (ec, size_a);
			}
		}
	};
	// clang-format on
}

std::string btcb::transport::channel_tcp::to_string () const
{
	return boost::str (boost::format ("%1%") % socket->remote_endpoint ());
}

btcb::transport::tcp_channels::tcp_channels (btcb::node & node_a) :
node (node_a)
{
}

bool btcb::transport::tcp_channels::insert (std::shared_ptr<btcb::transport::channel_tcp> channel_a)
{
	auto endpoint (channel_a->get_tcp_endpoint ());
	assert (endpoint.address ().is_v6 ());
	auto udp_endpoint (btcb::transport::map_tcp_to_endpoint (endpoint));
	bool error (true);
	if (!node.network.not_a_peer (udp_endpoint, node.config.allow_local_peers))
	{
		std::unique_lock<std::mutex> lock (mutex);
		auto existing (channels.get<endpoint_tag> ().find (endpoint));
		if (existing == channels.get<endpoint_tag> ().end ())
		{
			channels.get<endpoint_tag> ().insert ({ channel_a });
			error = false;
			lock.unlock ();
			node.network.channel_observer (channel_a);
			// Remove UDP channel to same IP:port if exists
			node.network.udp_channels.erase (udp_endpoint);
		}
	}
	return error;
}

void btcb::transport::tcp_channels::erase (btcb::tcp_endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	channels.get<endpoint_tag> ().erase (endpoint_a);
}

size_t btcb::transport::tcp_channels::size () const
{
	std::lock_guard<std::mutex> lock (mutex);
	return channels.size ();
}

std::shared_ptr<btcb::transport::channel_tcp> btcb::transport::tcp_channels::find_channel (btcb::tcp_endpoint const & endpoint_a) const
{
	std::lock_guard<std::mutex> lock (mutex);
	std::shared_ptr<btcb::transport::channel_tcp> result;
	auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

std::unordered_set<std::shared_ptr<btcb::transport::channel>> btcb::transport::tcp_channels::random_set (size_t count_a) const
{
	std::unordered_set<std::shared_ptr<btcb::transport::channel>> result;
	result.reserve (count_a);
	std::lock_guard<std::mutex> lock (mutex);
	// Stop trying to fill result with random samples after this many attempts
	auto random_cutoff (count_a * 2);
	auto peers_size (channels.size ());
	// Usually count_a will be much smaller than peers.size()
	// Otherwise make sure we have a cutoff on attempting to randomly fill
	if (!channels.empty ())
	{
		for (auto i (0); i < random_cutoff && result.size () < count_a; ++i)
		{
			auto index (btcb::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (peers_size - 1)));
			result.insert (channels.get<random_access_tag> ()[index].channel);
		}
	}
	return result;
}

void btcb::transport::tcp_channels::random_fill (std::array<btcb::endpoint, 8> & target_a) const
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

bool btcb::transport::tcp_channels::store_all (bool clear_peers)
{
	// We can't hold the mutex while starting a write transaction, so
	// we collect endpoints to be saved and then relase the lock.
	std::vector<btcb::endpoint> endpoints;
	{
		std::lock_guard<std::mutex> lock (mutex);
		endpoints.reserve (channels.size ());
		std::transform (channels.begin (), channels.end (),
		std::back_inserter (endpoints), [](const auto & channel) { return btcb::transport::map_tcp_to_endpoint (channel.endpoint ()); });
	}
	bool result (false);
	if (!endpoints.empty ())
	{
		// Clear all peers then refresh with the current list of peers
		auto transaction (node.store.tx_begin_write ());
		if (clear_peers)
		{
			node.store.peer_clear (transaction);
		}
		for (auto endpoint : endpoints)
		{
			btcb::endpoint_key endpoint_key (endpoint.address ().to_v6 ().to_bytes (), endpoint.port ());
			node.store.peer_put (transaction, std::move (endpoint_key));
		}
		result = true;
	}
	return result;
}

std::shared_ptr<btcb::transport::channel_tcp> btcb::transport::tcp_channels::find_node_id (btcb::account const & node_id_a)
{
	std::shared_ptr<btcb::transport::channel_tcp> result;
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<node_id_tag> ().find (node_id_a));
	if (existing != channels.get<node_id_tag> ().end ())
	{
		result = existing->channel;
	}
	return result;
}

btcb::tcp_endpoint btcb::transport::tcp_channels::bootstrap_peer ()
{
	btcb::tcp_endpoint result (boost::asio::ip::address_v6::any (), 0);
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (channels.get<last_bootstrap_attempt_tag> ().begin ()), n (channels.get<last_bootstrap_attempt_tag> ().end ()); i != n;)
	{
		if (i->channel->get_network_version () >= protocol_version_reasonable_min)
		{
			result = i->endpoint ();
			channels.get<last_bootstrap_attempt_tag> ().modify (i, [](channel_tcp_wrapper & wrapper_a) {
				wrapper_a.channel->set_last_bootstrap_attempt (std::chrono::steady_clock::now ());
			});
			i = n;
		}
		else
		{
			++i;
		}
	}
	return result;
}

void btcb::transport::tcp_channels::process_message (btcb::message const & message_a, btcb::tcp_endpoint const & endpoint_a, btcb::account const & node_id_a)
{
	if (!stopped)
	{
		auto channel (node.network.find_channel (btcb::transport::map_tcp_to_endpoint (endpoint_a)));
		if (channel)
		{
			node.process_message (message_a, channel);
		}
		else
		{
			channel = node.network.search_response_channel (endpoint_a, node_id_a);
			if (channel)
			{
				node.process_message (message_a, channel);
			}
			else
			{
				auto udp_channel (std::make_shared<btcb::transport::channel_udp> (node.network.udp_channels, btcb::transport::map_tcp_to_endpoint (endpoint_a)));
				node.process_message (message_a, udp_channel);
			}
		}
	}
}

void btcb::transport::tcp_channels::process_keepalive (btcb::keepalive const & message_a, btcb::tcp_endpoint const & endpoint_a, bool keepalive_first)
{
	if (!max_ip_connections (endpoint_a))
	{
		// Check for special node port data
		std::vector<btcb::tcp_endpoint> insert_response_channels;
		auto peer0 (message_a.peers[0]);
		auto peer1 (message_a.peers[1]);
		if (peer0.address () == boost::asio::ip::address_v6{} && peer0.port () != 0)
		{
			btcb::endpoint new_endpoint (endpoint_a.address (), peer0.port ());
			node.network.merge_peer (new_endpoint);
			if (keepalive_first)
			{
				insert_response_channels.push_back (btcb::transport::map_endpoint_to_tcp (new_endpoint));
			}
		}
		if (peer1.address () != boost::asio::ip::address_v6{} && peer1.port () != 0 && keepalive_first)
		{
			insert_response_channels.push_back (btcb::transport::map_endpoint_to_tcp (peer1));
		}
		// Insert preferred response channels from first TCP keepalive
		if (!insert_response_channels.empty ())
		{
			node.network.add_response_channels (endpoint_a, insert_response_channels);
		}
		auto udp_channel (std::make_shared<btcb::transport::channel_udp> (node.network.udp_channels, btcb::transport::map_tcp_to_endpoint (endpoint_a)));
		node.process_message (message_a, udp_channel);
	}
}

void btcb::transport::tcp_channels::start ()
{
	ongoing_keepalive ();
	ongoing_syn_cookie_cleanup ();
}

void btcb::transport::tcp_channels::stop ()
{
	stopped = true;
	// Close all TCP sockets
	for (auto i (channels.begin ()), j (channels.end ()); i != j; ++i)
	{
		if (i->channel->socket != nullptr)
		{
			i->channel->socket->close ();
		}
	}
}

bool btcb::transport::tcp_channels::max_ip_connections (btcb::tcp_endpoint const & endpoint_a)
{
	std::unique_lock<std::mutex> lock (mutex);
	bool result (channels.get<ip_address_tag> ().count (endpoint_a.address ()) >= btcb::transport::max_peers_per_ip);
	return result;
}

bool btcb::transport::tcp_channels::reachout (btcb::endpoint const & endpoint_a)
{
	auto tcp_endpoint (btcb::transport::map_endpoint_to_tcp (endpoint_a));
	// Don't overload single IP
	bool error = max_ip_connections (tcp_endpoint);
	if (!error)
	{
		// Don't keepalive to nodes that already sent us something
		error |= find_channel (tcp_endpoint) != nullptr;
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (attempts.find (tcp_endpoint));
		error |= existing != attempts.end ();
		attempts.insert ({ tcp_endpoint, std::chrono::steady_clock::now () });
	}
	return error;
}

std::unique_ptr<btcb::seq_con_info_component> btcb::transport::tcp_channels::collect_seq_con_info (std::string const & name)
{
	size_t channels_count = 0;
	size_t attemps_count = 0;
	size_t syn_cookies_count = 0;
	size_t syn_cookies_per_ip_count = 0;
	{
		std::lock_guard<std::mutex> guard (mutex);
		channels_count = channels.size ();
		attemps_count = attempts.size ();
	}
	{
		std::lock_guard<std::mutex> syn_cookie_guard (syn_cookie_mutex);
		syn_cookies_count = syn_cookies.size ();
		syn_cookies_per_ip_count = syn_cookies_per_ip.size ();
	}

	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "channels", channels_count, sizeof (decltype (channels)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "attempts", attemps_count, sizeof (decltype (attempts)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "syn_cookies", syn_cookies_count, sizeof (decltype (syn_cookies)::value_type) }));
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "syn_cookies_per_ip", syn_cookies_per_ip_count, sizeof (decltype (syn_cookies_per_ip)::value_type) }));

	return composite;
}

void btcb::transport::tcp_channels::purge (std::chrono::steady_clock::time_point const & cutoff_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto disconnect_cutoff (channels.get<last_packet_sent_tag> ().lower_bound (cutoff_a));
	channels.get<last_packet_sent_tag> ().erase (channels.get<last_packet_sent_tag> ().begin (), disconnect_cutoff);
	// Remove keepalive attempt tracking for attempts older than cutoff
	auto attempts_cutoff (attempts.get<1> ().lower_bound (cutoff_a));
	attempts.get<1> ().erase (attempts.get<1> ().begin (), attempts_cutoff);
}

boost::optional<btcb::uint256_union> btcb::transport::tcp_channels::assign_syn_cookie (btcb::tcp_endpoint const & endpoint_a)
{
	auto ip_addr (endpoint_a.address ());
	assert (ip_addr.is_v6 ());
	std::lock_guard<std::mutex> lock (syn_cookie_mutex);
	unsigned & ip_cookies = syn_cookies_per_ip[ip_addr];
	boost::optional<btcb::uint256_union> result;
	if (ip_cookies < btcb::transport::max_peers_per_ip)
	{
		if (syn_cookies.find (endpoint_a) == syn_cookies.end ())
		{
			btcb::uint256_union query;
			random_pool::generate_block (query.bytes.data (), query.bytes.size ());
			syn_cookie_info info{ query, std::chrono::steady_clock::now () };
			syn_cookies[endpoint_a] = info;
			++ip_cookies;
			result = query;
		}
	}
	return result;
}

bool btcb::transport::tcp_channels::validate_syn_cookie (btcb::tcp_endpoint const & endpoint_a, btcb::account const & node_id, btcb::signature const & sig)
{
	auto ip_addr (endpoint_a.address ());
	assert (ip_addr.is_v6 ());
	std::lock_guard<std::mutex> lock (syn_cookie_mutex);
	auto result (true);
	auto cookie_it (syn_cookies.find (endpoint_a));
	if (cookie_it != syn_cookies.end () && !btcb::validate_message (node_id, cookie_it->second.cookie, sig))
	{
		result = false;
		syn_cookies.erase (cookie_it);
		unsigned & ip_cookies = syn_cookies_per_ip[ip_addr];
		if (ip_cookies > 0)
		{
			--ip_cookies;
		}
		else
		{
			assert (false && "More SYN cookies deleted than created for IP");
		}
	}
	return result;
}

void btcb::transport::tcp_channels::purge_syn_cookies (std::chrono::steady_clock::time_point const & cutoff_a)
{
	std::lock_guard<std::mutex> lock (syn_cookie_mutex);
	auto it (syn_cookies.begin ());
	while (it != syn_cookies.end ())
	{
		auto info (it->second);
		if (info.created_at < cutoff_a)
		{
			unsigned & per_ip = syn_cookies_per_ip[it->first.address ()];
			if (per_ip > 0)
			{
				--per_ip;
			}
			else
			{
				assert (false && "More SYN cookies deleted than created for IP");
			}
			it = syn_cookies.erase (it);
		}
		else
		{
			++it;
		}
	}
}

void btcb::transport::tcp_channels::ongoing_syn_cookie_cleanup ()
{
	purge_syn_cookies (std::chrono::steady_clock::now () - btcb::transport::syn_cookie_cutoff);
	std::weak_ptr<btcb::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + (btcb::transport::syn_cookie_cutoff * 2), [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.tcp_channels.ongoing_syn_cookie_cleanup ();
		}
	});
}

void btcb::transport::tcp_channels::ongoing_keepalive ()
{
	btcb::keepalive message;
	node.network.random_fill (message.peers);
	std::unique_lock<std::mutex> lock (mutex);
	// Wake up channels
	std::vector<std::shared_ptr<btcb::transport::channel_tcp>> send_list;
	auto keepalive_sent_cutoff (channels.get<last_packet_sent_tag> ().lower_bound (std::chrono::steady_clock::now () - node.network_params.node.period));
	for (auto i (channels.get<last_packet_sent_tag> ().begin ()); i != keepalive_sent_cutoff; ++i)
	{
		send_list.push_back (i->channel);
	}
	lock.unlock ();
	for (auto & channel : send_list)
	{
		std::weak_ptr<btcb::node> node_w (node.shared ());
		channel->send (message);
	}
	std::weak_ptr<btcb::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + node.network_params.node.period, [node_w]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->network.tcp_channels.ongoing_keepalive ();
		}
	});
}

void btcb::transport::tcp_channels::list (std::deque<std::shared_ptr<btcb::transport::channel>> & deque_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	for (auto i (channels.begin ()), j (channels.end ()); i != j; ++i)
	{
		deque_a.push_back (i->channel);
	}
}

void btcb::transport::tcp_channels::modify (std::shared_ptr<btcb::transport::channel_tcp> channel_a, std::function<void(std::shared_ptr<btcb::transport::channel_tcp>)> modify_callback_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<endpoint_tag> ().find (channel_a->get_tcp_endpoint ()));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		channels.get<endpoint_tag> ().modify (existing, [modify_callback_a](channel_tcp_wrapper & wrapper_a) {
			modify_callback_a (wrapper_a.channel);
		});
	}
}

void btcb::transport::tcp_channels::update (btcb::tcp_endpoint const & endpoint_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	auto existing (channels.get<endpoint_tag> ().find (endpoint_a));
	if (existing != channels.get<endpoint_tag> ().end ())
	{
		channels.get<endpoint_tag> ().modify (existing, [](channel_tcp_wrapper & wrapper_a) {
			wrapper_a.channel->set_last_packet_sent (std::chrono::steady_clock::now ());
		});
	}
}

void btcb::transport::tcp_channels::start_tcp (btcb::endpoint const & endpoint_a, std::function<void(std::shared_ptr<btcb::transport::channel>)> const & callback_a)
{
	auto socket (std::make_shared<btcb::socket> (node.shared_from_this (), boost::none, btcb::socket::concurrency::multi_writer));
	auto channel (std::make_shared<btcb::transport::channel_tcp> (node, socket));
	std::weak_ptr<btcb::node> node_w (node.shared ());
	channel->socket->async_connect (btcb::transport::map_endpoint_to_tcp (endpoint_a),
	[node_w, channel, endpoint_a, callback_a](boost::system::error_code const & ec) {
		if (auto node_l = node_w.lock ())
		{
			if (!ec && channel)
			{
				// TCP node ID handshake
				auto cookie (node_l->network.tcp_channels.assign_syn_cookie (btcb::transport::map_endpoint_to_tcp (endpoint_a)));
				btcb::node_id_handshake message (cookie, boost::none);
				auto bytes = message.to_bytes ();
				if (node_l->config.logging.network_node_id_handshake_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Node ID handshake request sent with node ID %1% to %2%: query %3%") % node_l->node_id.pub.to_account () % endpoint_a % (*cookie).to_string ()));
				}
				std::shared_ptr<std::vector<uint8_t>> receive_buffer (std::make_shared<std::vector<uint8_t>> ());
				receive_buffer->resize (256);
				channel->send_buffer (bytes, btcb::stat::detail::node_id_handshake, [node_w, channel, endpoint_a, receive_buffer, callback_a](boost::system::error_code const & ec, size_t size_a) {
					if (auto node_l = node_w.lock ())
					{
						if (!ec && channel)
						{
							node_l->network.tcp_channels.start_tcp_receive_node_id (channel, endpoint_a, receive_buffer, callback_a);
						}
						else
						{
							if (node_l->config.logging.network_node_id_handshake_logging ())
							{
								node_l->logger.try_log (boost::str (boost::format ("Error sending node_id_handshake to %1%: %2%") % endpoint_a % ec.message ()));
							}
							node_l->network.tcp_channels.udp_fallback (endpoint_a, callback_a);
						}
					}
				});
			}
			else
			{
				node_l->network.tcp_channels.udp_fallback (endpoint_a, callback_a);
			}
		}
	});
}

void btcb::transport::tcp_channels::start_tcp_receive_node_id (std::shared_ptr<btcb::transport::channel_tcp> channel_a, btcb::endpoint const & endpoint_a, std::shared_ptr<std::vector<uint8_t>> receive_buffer_a, std::function<void(std::shared_ptr<btcb::transport::channel>)> const & callback_a)
{
	std::weak_ptr<btcb::node> node_w (node.shared ());
	channel_a->socket->async_read (receive_buffer_a, 8 + sizeof (btcb::account) + sizeof (btcb::account) + sizeof (btcb::signature), [node_w, channel_a, endpoint_a, receive_buffer_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
		if (auto node_l = node_w.lock ())
		{
			if (!ec && channel_a)
			{
				node_l->stats.inc (btcb::stat::type::message, btcb::stat::detail::node_id_handshake, btcb::stat::dir::in);
				auto error (false);
				btcb::bufferstream stream (receive_buffer_a->data (), size_a);
				btcb::message_header header (error, stream);
				if (!error && header.type == btcb::message_type::node_id_handshake && header.version_using >= btcb::protocol_version_min)
				{
					btcb::node_id_handshake message (error, stream, header);
					if (!error && message.response && message.query)
					{
						channel_a->set_network_version (header.version_using);
						auto node_id (message.response->first);
						if (!node_l->network.tcp_channels.validate_syn_cookie (btcb::transport::map_endpoint_to_tcp (endpoint_a), node_id, message.response->second) && node_id != node_l->node_id.pub && !node_l->network.tcp_channels.find_node_id (node_id))
						{
							channel_a->set_node_id (node_id);
							channel_a->set_last_packet_received (std::chrono::steady_clock::now ());
							boost::optional<std::pair<btcb::account, btcb::signature>> response (std::make_pair (node_l->node_id.pub, btcb::sign_message (node_l->node_id.prv, node_l->node_id.pub, *message.query)));
							btcb::node_id_handshake response_message (boost::none, response);
							auto bytes = response_message.to_bytes ();
							if (node_l->config.logging.network_node_id_handshake_logging ())
							{
								node_l->logger.try_log (boost::str (boost::format ("Node ID handshake response sent with node ID %1% to %2%: query %3%") % node_l->node_id.pub.to_account () % endpoint_a % (*message.query).to_string ()));
							}
							channel_a->send_buffer (bytes, btcb::stat::detail::node_id_handshake, [node_w, channel_a, endpoint_a, callback_a](boost::system::error_code const & ec, size_t size_a) {
								if (auto node_l = node_w.lock ())
								{
									if (!ec && channel_a)
									{
										// Insert new node ID connection
										channel_a->set_last_packet_sent (std::chrono::steady_clock::now ());
										node_l->network.tcp_channels.insert (channel_a);
										if (callback_a)
										{
											callback_a (channel_a);
										}
									}
									else
									{
										if (node_l->config.logging.network_node_id_handshake_logging ())
										{
											node_l->logger.try_log (boost::str (boost::format ("Error sending node_id_handshake to %1%: %2%") % endpoint_a % ec.message ()));
										}
										node_l->network.tcp_channels.udp_fallback (endpoint_a, callback_a);
									}
								}
							});
						}
						// If node ID is known, don't establish new connection
					}
					else
					{
						node_l->network.tcp_channels.udp_fallback (endpoint_a, callback_a);
					}
				}
				else
				{
					node_l->network.tcp_channels.udp_fallback (endpoint_a, callback_a);
				}
			}
			else
			{
				if (node_l->config.logging.network_node_id_handshake_logging ())
				{
					node_l->logger.try_log (boost::str (boost::format ("Error reading node_id_handshake from %1%: %2%") % endpoint_a % ec.message ()));
				}
				node_l->network.tcp_channels.udp_fallback (endpoint_a, callback_a);
			}
		}
	});
}

void btcb::transport::tcp_channels::udp_fallback (btcb::endpoint const & endpoint_a, std::function<void(std::shared_ptr<btcb::transport::channel>)> const & callback_a)
{
	if (callback_a)
	{
		auto channel_udp (node.network.udp_channels.create (endpoint_a));
		callback_a (channel_udp);
	}
}
