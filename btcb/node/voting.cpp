#include <btcb/node/voting.hpp>

#include <btcb/node/node.hpp>

btcb::vote_generator::vote_generator (btcb::node & node_a) :
node (node_a),
stopped (false),
started (false),
thread ([this]() { run (); })
{
	std::unique_lock<std::mutex> lock (mutex);
	while (!started)
	{
		condition.wait (lock);
	}
}

void btcb::vote_generator::add (btcb::block_hash const & hash_a)
{
	{
		std::lock_guard<std::mutex> lock (mutex);
		hashes.push_back (hash_a);
	}
	condition.notify_all ();
}

void btcb::vote_generator::stop ()
{
	std::unique_lock<std::mutex> lock (mutex);
	stopped = true;

	lock.unlock ();
	condition.notify_all ();

	if (thread.joinable ())
	{
		thread.join ();
	}
}

void btcb::vote_generator::send (std::unique_lock<std::mutex> & lock_a)
{
	std::vector<btcb::block_hash> hashes_l;
	hashes_l.reserve (12);
	while (!hashes.empty () && hashes_l.size () < 12)
	{
		hashes_l.push_back (hashes.front ());
		hashes.pop_front ();
	}
	lock_a.unlock ();
	{
		auto transaction (node.store.tx_begin_read ());
		node.wallets.foreach_representative (transaction, [this, &hashes_l, &transaction](btcb::public_key const & pub_a, btcb::raw_key const & prv_a) {
			auto vote (this->node.store.vote_generate (transaction, pub_a, prv_a, hashes_l));
			this->node.vote_processor.vote (vote, std::make_shared<btcb::transport::channel_udp> (this->node.network.udp_channels, this->node.network.endpoint ()));
			this->node.votes_cache.add (vote);
		});
	}
	lock_a.lock ();
}

void btcb::vote_generator::run ()
{
	btcb::thread_role::set (btcb::thread_role::name::voting);
	std::unique_lock<std::mutex> lock (mutex);
	started = true;
	lock.unlock ();
	condition.notify_all ();
	lock.lock ();
	while (!stopped)
	{
		if (!hashes.empty ())
		{
			send (lock);
		}
		else
		{
			condition.wait (lock);
		}
	}
}

void btcb::votes_cache::add (std::shared_ptr<btcb::vote> const & vote_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	for (auto & block : vote_a->blocks)
	{
		auto hash (boost::get<btcb::block_hash> (block));
		auto existing (cache.get<1> ().find (hash));
		if (existing == cache.get<1> ().end ())
		{
			// Clean old votes
			if (cache.size () >= network_params.voting.max_cache)
			{
				cache.erase (cache.begin ());
			}
			// Insert new votes (new hash)
			auto inserted (cache.insert (btcb::cached_votes{ std::chrono::steady_clock::now (), hash, std::vector<std::shared_ptr<btcb::vote>> (1, vote_a) }));
			assert (inserted.second);
		}
		else
		{
			// Insert new votes (old hash)
			cache.get<1> ().modify (existing, [vote_a](btcb::cached_votes & cache_a) {
				cache_a.votes.push_back (vote_a);
			});
		}
	}
}

std::vector<std::shared_ptr<btcb::vote>> btcb::votes_cache::find (btcb::block_hash const & hash_a)
{
	std::vector<std::shared_ptr<btcb::vote>> result;
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto existing (cache.get<1> ().find (hash_a));
	if (existing != cache.get<1> ().end ())
	{
		result = existing->votes;
	}
	return result;
}

void btcb::votes_cache::remove (btcb::block_hash const & hash_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	cache.get<1> ().erase (hash_a);
}

namespace btcb
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_generator & vote_generator, const std::string & name)
{
	size_t hashes_count = 0;

	{
		std::lock_guard<std::mutex> guard (vote_generator.mutex);
		hashes_count = vote_generator.hashes.size ();
	}
	auto sizeof_element = sizeof (decltype (vote_generator.hashes)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "state_blocks", hashes_count, sizeof_element }));
	return composite;
}

std::unique_ptr<seq_con_info_component> collect_seq_con_info (votes_cache & votes_cache, const std::string & name)
{
	size_t cache_count = 0;

	{
		std::lock_guard<std::mutex> guard (votes_cache.cache_mutex);
		cache_count = votes_cache.cache.size ();
	}
	auto sizeof_element = sizeof (decltype (votes_cache.cache)::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	/* This does not currently loop over each element inside the cache to get the sizes of the votes inside cached_votes */
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "cache", cache_count, sizeof_element }));
	return composite;
}
}
