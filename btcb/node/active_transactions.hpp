#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <queue>
#include <unordered_map>

#include <btcb/lib/numbers.hpp>
#include <btcb/secure/common.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/thread.hpp>

namespace btcb
{
class node;
class block;
class vote;
class election;
class transaction;

class conflict_info final
{
public:
	btcb::qualified_root root;
	uint64_t difficulty;
	uint64_t adjusted_difficulty;
	std::shared_ptr<btcb::election> election;
};

class election_status final
{
public:
	std::shared_ptr<btcb::block> winner;
	btcb::amount tally;
	std::chrono::milliseconds election_end;
	std::chrono::milliseconds election_duration;
};

class transaction_counter final
{
public:
	// increment counter
	void add ();
	// clear counter and reset trend_last after calculating a new rate, guarded to only run once a sec
	void trend_sample ();
	double get_rate ();

private:
	std::chrono::steady_clock::time_point trend_last = std::chrono::steady_clock::now ();
	size_t counter = 0;
	// blocks/sec confirmed
	double rate = 0;
	std::mutex mutex;
};

// Core class for determining consensus
// Holds all active blocks i.e. recently added blocks that need confirmation
class active_transactions final
{
public:
	explicit active_transactions (btcb::node &, bool delay_frontier_confirmation_height_updating = false);
	~active_transactions ();
	// Start an election for a block
	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	bool start (std::shared_ptr<btcb::block>, std::function<void(std::shared_ptr<btcb::block>)> const & = [](std::shared_ptr<btcb::block>) {});
	// clang-format on
	// If this returns true, the vote is a replay
	// If this returns false, the vote may or may not be a replay
	bool vote (std::shared_ptr<btcb::vote>, bool = false);
	// Is the root of this block in the roots container
	bool active (btcb::block const &);
	bool active (btcb::qualified_root const &);
	void update_difficulty (btcb::block const &);
	void adjust_difficulty (btcb::block_hash const &);
	void update_active_difficulty (std::unique_lock<std::mutex> &);
	uint64_t active_difficulty ();
	std::deque<std::shared_ptr<btcb::block>> list_blocks (bool = false);
	void erase (btcb::block const &);
	//check if we should flush
	//if counter.rate == 0 set minimum_size before considering flushing to 4 for testing convenience
	//else minimum_size is rate * 10
	//when roots.size > minimum_size check counter.rate and adjusted expected percentage long unconfirmed before kicking in
	bool should_flush ();
	//drop 2 from roots based on adjusted_difficulty
	void flush_lowest ();
	bool empty ();
	size_t size ();
	void stop ();
	bool publish (std::shared_ptr<btcb::block> block_a);
	void confirm_block (btcb::block_hash const &);
	boost::multi_index_container<
	btcb::conflict_info,
	boost::multi_index::indexed_by<
	boost::multi_index::hashed_unique<
	boost::multi_index::member<btcb::conflict_info, btcb::qualified_root, &btcb::conflict_info::root>>,
	boost::multi_index::ordered_non_unique<
	boost::multi_index::member<btcb::conflict_info, uint64_t, &btcb::conflict_info::adjusted_difficulty>,
	std::greater<uint64_t>>>>
	roots;
	std::unordered_map<btcb::block_hash, std::shared_ptr<btcb::election>> blocks;
	std::deque<btcb::election_status> list_confirmed ();
	std::deque<btcb::election_status> confirmed;
	btcb::transaction_counter counter;
	btcb::node & node;
	std::mutex mutex;
	// Maximum number of conflicts to vote on per interval, lowest root hash first
	static unsigned constexpr announcements_per_interval = 32;
	// Minimum number of block announcements
	static unsigned constexpr announcement_min = 2;
	// Threshold to start logging blocks haven't yet been confirmed
	static unsigned constexpr announcement_long = 20;
	size_t long_unconfirmed_size = 0;
	static size_t constexpr election_history_size = 2048;
	static size_t constexpr max_broadcast_queue = 1000;
	boost::circular_buffer<double> multipliers_cb;
	uint64_t trended_active_difficulty;

private:
	// Call action with confirmed block, may be different than what we started with
	// clang-format off
	bool add (std::shared_ptr<btcb::block>, std::function<void(std::shared_ptr<btcb::block>)> const & = [](std::shared_ptr<btcb::block>) {});
	// clang-format on
	void request_loop ();
	void request_confirm (std::unique_lock<std::mutex> &);
	void confirm_frontiers (btcb::transaction const &);
	btcb::account next_frontier_account{ 0 };
	std::chrono::steady_clock::time_point next_frontier_check{ std::chrono::steady_clock::now () };
	std::condition_variable condition;
	bool started{ false };
	std::atomic<bool> stopped{ false };
	static size_t constexpr confirmed_frontiers_max_pending_cut_off = 100;
	boost::thread thread;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (active_transactions & active_transactions, const std::string & name);
}
