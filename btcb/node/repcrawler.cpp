#include <btcb/node/node.hpp>
#include <btcb/node/repcrawler.hpp>

btcb::rep_crawler::rep_crawler (btcb::node & node_a) :
node (node_a)
{
	node.observers.endpoint.add ([this](std::shared_ptr<btcb::transport::channel> channel_a) {
		this->query (channel_a);
	});
}

void btcb::rep_crawler::add (btcb::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (active_mutex);
	active.insert (hash_a);
}

void btcb::rep_crawler::remove (btcb::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (active_mutex);
	active.erase (hash_a);
}

bool btcb::rep_crawler::exists (btcb::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (active_mutex);
	return active.count (hash_a) != 0;
}

void btcb::rep_crawler::start ()
{
	ongoing_crawl ();
}

void btcb::rep_crawler::ongoing_crawl ()
{
	auto now (std::chrono::steady_clock::now ());
	auto total_weight_l (total_weight ());
	query (get_crawl_targets (total_weight_l));
	auto sufficient_weight (total_weight_l > node.config.online_weight_minimum.number ());
	// If online weight drops below minimum, reach out to preconfigured peers
	if (!sufficient_weight)
	{
		node.keepalive_preconfigured (node.config.preconfigured_peers);
	}
	// Reduce crawl frequency when there's enough total peer weight
	unsigned next_run_seconds = sufficient_weight ? 7 : 3;
	std::weak_ptr<btcb::node> node_w (node.shared ());
	node.alarm.add (now + std::chrono::seconds (next_run_seconds), [node_w, this]() {
		if (auto node_l = node_w.lock ())
		{
			this->ongoing_crawl ();
		}
	});
}

std::vector<std::shared_ptr<btcb::transport::channel>> btcb::rep_crawler::get_crawl_targets (btcb::uint128_t total_weight_a)
{
	std::unordered_set<std::shared_ptr<btcb::transport::channel>> channels;
	constexpr size_t conservative_count = 10;
	constexpr size_t aggressive_count = 40;

	// Crawl more aggressively if we lack sufficient total peer weight.
	bool sufficient_weight (total_weight_a > node.config.online_weight_minimum.number ());
	uint16_t required_peer_count = sufficient_weight ? conservative_count : aggressive_count;
	std::lock_guard<std::mutex> lock (probable_reps_mutex);

	// First, add known rep endpoints, ordered by ascending last-requested time.
	for (auto i (probable_reps.get<tag_last_request> ().begin ()), n (probable_reps.get<tag_last_request> ().end ()); i != n && channels.size () < required_peer_count; ++i)
	{
		channels.insert (i->channel);
	};

	// Add additional random peers. We do this even if we have enough weight, in order to pick up reps
	// that didn't respond when first observed. If the current total weight isn't sufficient, this
	// will be more aggressive. When the node first starts, the rep container is empty and all
	// endpoints will originate from random peers.
	required_peer_count += required_peer_count / 2;

	// The rest of the endpoints are picked randomly
	auto random_peers (node.network.random_set (required_peer_count));
	std::vector<std::shared_ptr<btcb::transport::channel>> result;
	result.insert (result.end (), random_peers.begin (), random_peers.end ());
	return result;
}

void btcb::rep_crawler::query (std::vector<std::shared_ptr<btcb::transport::channel>> const & channels_a)
{
	auto transaction (node.store.tx_begin_read ());
	std::shared_ptr<btcb::block> block (node.store.block_random (transaction));
	auto hash (block->hash ());
	// Don't send same block multiple times in tests
	if (node.network_params.network.is_test_network ())
	{
		for (auto i (0); exists (hash) && i < 4; ++i)
		{
			block = node.store.block_random (transaction);
			hash = block->hash ();
		}
	}
	add (hash);
	for (auto i (channels_a.begin ()), n (channels_a.end ()); i != n; ++i)
	{
		on_rep_request (*i);
		btcb::confirm_req message (block);
		(*i)->send (message);
	}

	// A representative must respond with a vote within the deadline
	std::weak_ptr<btcb::node> node_w (node.shared ());
	node.alarm.add (std::chrono::steady_clock::now () + std::chrono::seconds (5), [node_w, hash]() {
		if (auto node_l = node_w.lock ())
		{
			node_l->rep_crawler.remove (hash);
		}
	});
}

void btcb::rep_crawler::query (std::shared_ptr<btcb::transport::channel> channel_a)
{
	std::vector<std::shared_ptr<btcb::transport::channel>> peers;
	peers.push_back (channel_a);
	query (peers);
}

bool btcb::rep_crawler::response (std::shared_ptr<btcb::transport::channel> channel_a, btcb::account const & rep_account_a, btcb::amount const & weight_a)
{
	auto updated (false);
	std::lock_guard<std::mutex> lock (probable_reps_mutex);
	auto existing (probable_reps.find (rep_account_a));
	if (existing != probable_reps.end ())
	{
		probable_reps.modify (existing, [weight_a, &updated, rep_account_a, channel_a](btcb::representative & info) {
			info.last_response = std::chrono::steady_clock::now ();

			if (info.weight < weight_a)
			{
				updated = true;
				info.weight = weight_a;
				info.channel = channel_a;
				info.account = rep_account_a;
			}
		});
	}
	else
	{
		probable_reps.insert (btcb::representative (rep_account_a, weight_a, channel_a));
	}
	return updated;
}

btcb::uint128_t btcb::rep_crawler::total_weight () const
{
	std::lock_guard<std::mutex> lock (probable_reps_mutex);
	btcb::uint128_t result (0);
	for (auto i (probable_reps.get<tag_weight> ().begin ()), n (probable_reps.get<tag_weight> ().end ()); i != n; ++i)
	{
		auto weight (i->weight.number ());
		if (weight > 0)
		{
			result = result + weight;
		}
		else
		{
			break;
		}
	}
	return result;
}

std::vector<btcb::representative> btcb::rep_crawler::representatives_by_weight ()
{
	std::vector<btcb::representative> result;
	std::lock_guard<std::mutex> lock (probable_reps_mutex);
	for (auto i (probable_reps.get<tag_weight> ().begin ()), n (probable_reps.get<tag_weight> ().end ()); i != n; ++i)
	{
		auto weight (i->weight.number ());
		if (weight > 0)
		{
			result.push_back (*i);
		}
		else
		{
			break;
		}
	}
	return result;
}

void btcb::rep_crawler::on_rep_request (std::shared_ptr<btcb::transport::channel> channel_a)
{
	std::lock_guard<std::mutex> lock (probable_reps_mutex);

	using probable_rep_itr_t = probably_rep_t::index<tag_channel_ref>::type::iterator;
	probably_rep_t::index<tag_channel_ref>::type & channel_ref_index = probable_reps.get<tag_channel_ref> ();

	// Find and update the timestamp on all reps available on the endpoint (a single host may have multiple reps)
	std::vector<probable_rep_itr_t> view;
	auto itr_pair = probable_reps.get<tag_channel_ref> ().equal_range (*channel_a);
	for (; itr_pair.first != itr_pair.second; itr_pair.first++)
	{
		channel_ref_index.modify (itr_pair.first, [](btcb::representative & value_a) {
			value_a.last_request = std::chrono::steady_clock::now ();
		});
	}
}

std::vector<btcb::representative> btcb::rep_crawler::representatives (size_t count_a)
{
	std::vector<representative> result;
	result.reserve (std::min (count_a, size_t (16)));
	std::lock_guard<std::mutex> lock (probable_reps_mutex);
	for (auto i (probable_reps.get<tag_weight> ().begin ()), n (probable_reps.get<tag_weight> ().end ()); i != n && result.size () < count_a; ++i)
	{
		if (!i->weight.is_zero ())
		{
			result.push_back (*i);
		}
	}
	return result;
}

std::vector<std::shared_ptr<btcb::transport::channel>> btcb::rep_crawler::representative_endpoints (size_t count_a)
{
	std::vector<std::shared_ptr<btcb::transport::channel>> result;
	auto reps (representatives (count_a));
	for (auto rep : reps)
	{
		result.push_back (rep.channel);
	}
	return result;
}

/** Total number of representatives */
size_t btcb::rep_crawler::representative_count ()
{
	std::lock_guard<std::mutex> lock (probable_reps_mutex);
	return probable_reps.size ();
}
