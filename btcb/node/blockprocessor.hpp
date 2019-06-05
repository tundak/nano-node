#pragma once

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <chrono>
#include <memory>
#include <btcb/lib/blocks.hpp>
#include <btcb/node/voting.hpp>
#include <btcb/secure/common.hpp>
#include <unordered_set>

namespace btcb
{
class node;
class transaction;

class rolled_hash
{
public:
	std::chrono::steady_clock::time_point time;
	btcb::block_hash hash;
};
/**
 * Processing blocks is a potentially long IO operation.
 * This class isolates block insertion from other operations like servicing network operations
 */
class block_processor final
{
public:
	explicit block_processor (btcb::node &);
	~block_processor ();
	void stop ();
	void flush ();
	bool full ();
	void add (btcb::unchecked_info const &);
	void add (std::shared_ptr<btcb::block>, uint64_t = 0);
	void force (std::shared_ptr<btcb::block>);
	bool should_log (bool);
	bool have_blocks ();
	void process_blocks ();
	btcb::process_return process_one (btcb::transaction const &, btcb::unchecked_info);
	btcb::process_return process_one (btcb::transaction const &, std::shared_ptr<btcb::block>);
	btcb::vote_generator generator;
	// Delay required for average network propagartion before requesting confirmation
	static std::chrono::milliseconds constexpr confirmation_request_delay{ 1500 };

private:
	void queue_unchecked (btcb::transaction const &, btcb::block_hash const &);
	void verify_state_blocks (btcb::transaction const & transaction_a, std::unique_lock<std::mutex> &, size_t = std::numeric_limits<size_t>::max ());
	void process_batch (std::unique_lock<std::mutex> &);
	void process_live (btcb::block_hash const &, std::shared_ptr<btcb::block>);
	bool stopped;
	bool active;
	std::chrono::steady_clock::time_point next_log;
	std::deque<btcb::unchecked_info> state_blocks;
	std::deque<btcb::unchecked_info> blocks;
	std::unordered_set<btcb::block_hash> blocks_hashes;
	std::deque<std::shared_ptr<btcb::block>> forced;
	boost::multi_index_container<
	btcb::rolled_hash,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<btcb::rolled_hash, std::chrono::steady_clock::time_point, &btcb::rolled_hash::time>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<btcb::rolled_hash, btcb::block_hash, &btcb::rolled_hash::hash>>>>
	rolled_back;
	static size_t const rolled_back_max = 1024;
	std::condition_variable condition;
	btcb::node & node;
	std::mutex mutex;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_processor & block_processor, const std::string & name);
};
}
