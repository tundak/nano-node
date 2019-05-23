#pragma once

#include <btcb/lib/config.hpp>
#include <btcb/lib/numbers.hpp>
#include <btcb/lib/utility.hpp>
#include <btcb/secure/common.hpp>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread.hpp>

#include <condition_variable>
#include <deque>
#include <mutex>

namespace btcb
{
class node;
class vote_generator final
{
public:
	vote_generator (btcb::node &);
	void add (btcb::block_hash const &);
	void stop ();

private:
	void run ();
	void send (std::unique_lock<std::mutex> &);
	btcb::node & node;
	std::mutex mutex;
	std::condition_variable condition;
	std::deque<btcb::block_hash> hashes;
	btcb::network_params network_params;
	bool stopped;
	bool started;
	boost::thread thread;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_generator & vote_generator, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_generator & vote_generator, const std::string & name);
class cached_votes final
{
public:
	std::chrono::steady_clock::time_point time;
	btcb::block_hash hash;
	std::vector<std::shared_ptr<btcb::vote>> votes;
};
class votes_cache final
{
public:
	void add (std::shared_ptr<btcb::vote> const &);
	std::vector<std::shared_ptr<btcb::vote>> find (btcb::block_hash const &);
	void remove (btcb::block_hash const &);

private:
	std::mutex cache_mutex;
	boost::multi_index_container<
	btcb::cached_votes,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<btcb::cached_votes, std::chrono::steady_clock::time_point, &btcb::cached_votes::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<btcb::cached_votes, btcb::block_hash, &btcb::cached_votes::hash>>>>
	cache;
	btcb::network_params network_params;
	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (votes_cache & votes_cache, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (votes_cache & votes_cache, const std::string & name);
}
