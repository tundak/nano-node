#pragma once

#include <boost/property_tree/ptree.hpp>
#include <boost/stacktrace/stacktrace_fwd.hpp>
#include <mutex>
#include <btcb/lib/timer.hpp>
#include <btcb/node/diagnosticsconfig.hpp>

namespace btcb
{
class transaction_impl;
class logger_mt;

class mdb_txn_stats
{
public:
	mdb_txn_stats (const btcb::transaction_impl * transaction_impl_a);
	bool is_write () const;
	btcb::timer<std::chrono::milliseconds> timer;
	const btcb::transaction_impl * transaction_impl;
	std::string thread_name;

	// Smart pointer so that we don't need the full definition which causes min/max issues on Windows
	std::shared_ptr<boost::stacktrace::stacktrace> stacktrace;
};

class mdb_txn_tracker
{
public:
	mdb_txn_tracker (btcb::logger_mt & logger_a, btcb::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a);
	void serialize_json (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time);
	void add (const btcb::transaction_impl * transaction_impl);
	void erase (const btcb::transaction_impl * transaction_impl);

private:
	std::mutex mutex;
	std::vector<mdb_txn_stats> stats;
	btcb::logger_mt & logger;
	btcb::txn_tracking_config txn_tracking_config;
	std::chrono::milliseconds block_processor_batch_max_time;

	void output_finished (btcb::mdb_txn_stats const & mdb_txn_stats) const;
};
}
