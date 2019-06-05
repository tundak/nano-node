#include <btcb/node/lmdb_txn_tracker.hpp>

#include <boost/polymorphic_cast.hpp>
#include <btcb/lib/jsonconfig.hpp>
#include <btcb/lib/logger_mt.hpp>
#include <btcb/lib/utility.hpp>
#include <btcb/secure/blockstore.hpp>

// Some builds (mac) fail due to "Boost.Stacktrace requires `_Unwind_Backtrace` function".
#ifndef _WIN32
#ifndef _GNU_SOURCE
#define BEFORE_GNU_SOURCE 0
#define _GNU_SOURCE
#else
#define BEFORE_GNU_SOURCE 1
#endif
#endif
// On Windows this include defines min/max macros, so keep below other includes
// to reduce conflicts with other std functions
#include <boost/stacktrace.hpp>
#ifndef _WIN32
#if !BEFORE_GNU_SOURCE
#undef _GNU_SOURCE
#endif
#endif

namespace
{
class matches_txn
{
public:
	matches_txn (const btcb::transaction_impl * transaction_impl_a) :
	transaction_impl (transaction_impl_a)
	{
	}

	bool operator() (btcb::mdb_txn_stats const & mdb_txn_stats)
	{
		return (mdb_txn_stats.transaction_impl == transaction_impl);
	}

private:
	const btcb::transaction_impl * transaction_impl;
};
}

btcb::mdb_txn_tracker::mdb_txn_tracker (btcb::logger_mt & logger_a, btcb::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a) :
logger (logger_a),
txn_tracking_config (txn_tracking_config_a),
block_processor_batch_max_time (block_processor_batch_max_time_a)
{
}

void btcb::mdb_txn_tracker::serialize_json (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time)
{
	// Copying is cheap compared to generating the stack trace strings, so reduce time holding the mutex
	std::vector<mdb_txn_stats> copy_stats;
	{
		std::lock_guard<std::mutex> guard (mutex);
		copy_stats = stats;
	}

	// Get the time difference now as creating stacktraces (Debug/Windows for instance) can take a while so results won't be as accurate
	std::vector<std::chrono::milliseconds> times_since_start;
	times_since_start.reserve (copy_stats.size ());
	// clang-format off
	std::transform (copy_stats.cbegin (), copy_stats.cend (), std::back_inserter (times_since_start), [] (const auto & stat) {
		return stat.timer.since_start ();
	});
	// clang-format on
	assert (times_since_start.size () == copy_stats.size ());

	for (size_t i = 0; i < times_since_start.size (); ++i)
	{
		auto const & stat = copy_stats[i];
		auto time_held_open = times_since_start[i];

		if ((stat.is_write () && time_held_open >= min_write_time) || (!stat.is_write () && time_held_open >= min_read_time))
		{
			btcb::jsonconfig mdb_lock_config;

			mdb_lock_config.put ("thread", stat.thread_name);
			mdb_lock_config.put ("time_held_open", time_held_open.count ());
			mdb_lock_config.put ("write", stat.is_write ());

			boost::property_tree::ptree stacktrace_config;
			for (auto frame : *stat.stacktrace)
			{
				btcb::jsonconfig frame_json;
				frame_json.put ("name", frame.name ());
				frame_json.put ("address", frame.address ());
				frame_json.put ("source_file", frame.source_file ());
				frame_json.put ("source_line", frame.source_line ());
				stacktrace_config.push_back (std::make_pair ("", frame_json.get_tree ()));
			}

			btcb::jsonconfig stack (stacktrace_config);
			mdb_lock_config.put_child ("stacktrace", stack);
			json.push_back (std::make_pair ("", mdb_lock_config.get_tree ()));
		}
	}
}

void btcb::mdb_txn_tracker::output_finished (btcb::mdb_txn_stats const & mdb_txn_stats) const
{
	// Only output them if transactions were held for longer than a certain period of time
	auto is_write = mdb_txn_stats.is_write ();
	auto time_open = mdb_txn_stats.timer.since_start ();

	auto should_ignore = false;
	// Reduce noise in log files by removing any entries from the block processor (if enabled) which are less than the max batch time (+ a few second buffer) because these are expected writes during bootstrapping.
	auto is_below_max_time = time_open <= (block_processor_batch_max_time + std::chrono::seconds (3));
	bool is_blk_processing_thread = mdb_txn_stats.thread_name == btcb::thread_role::get_string (btcb::thread_role::name::block_processing);
	if (txn_tracking_config.ignore_writes_below_block_processor_max_time && is_blk_processing_thread && is_write && is_below_max_time)
	{
		should_ignore = true;
	}

	if (!should_ignore && ((is_write && time_open >= txn_tracking_config.min_write_txn_time) || (!is_write && time_open >= txn_tracking_config.min_read_txn_time)))
	{
		assert (mdb_txn_stats.stacktrace);
		logger.always_log (boost::str (boost::format ("%1%ms %2% held on thread %3%\n%4%") % mdb_txn_stats.timer.since_start ().count () % (is_write ? "write lock" : "read") % mdb_txn_stats.thread_name % *mdb_txn_stats.stacktrace));
	}
}

void btcb::mdb_txn_tracker::add (const btcb::transaction_impl * transaction_impl)
{
	std::lock_guard<std::mutex> guard (mutex);
	// clang-format off
	assert (std::find_if (stats.cbegin (), stats.cend (), matches_txn (transaction_impl)) == stats.cend ());
	// clang-format on
	stats.emplace_back (transaction_impl);
}

/** Can be called without error if transaction does not exist */
void btcb::mdb_txn_tracker::erase (const btcb::transaction_impl * transaction_impl)
{
	std::lock_guard<std::mutex> guard (mutex);
	// clang-format off
	auto it = std::find_if (stats.begin (), stats.end (), matches_txn (transaction_impl));
	// clang-format on
	if (it != stats.end ())
	{
		output_finished (*it);
		it->timer.stop ();
		stats.erase (it);
	}
}

btcb::mdb_txn_stats::mdb_txn_stats (const btcb::transaction_impl * transaction_impl) :
transaction_impl (transaction_impl),
thread_name (btcb::thread_role::get_string ()),
stacktrace (std::make_shared<boost::stacktrace::stacktrace> ())
{
	timer.start ();
}

bool btcb::mdb_txn_stats::is_write () const
{
	return (dynamic_cast<const btcb::write_transaction_impl *> (transaction_impl) != nullptr);
}
