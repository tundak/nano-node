#pragma once

#include <condition_variable>
#include <mutex>
#include <btcb/lib/numbers.hpp>
#include <btcb/secure/common.hpp>
#include <thread>
#include <unordered_set>

namespace btcb
{
class block_store;
class stat;
class active_transactions;
class read_transaction;
class logger_mt;

class pending_confirmation_height
{
public:
	size_t size ();
	bool is_processing_block (btcb::block_hash const &);
	btcb::block_hash current ();

private:
	std::mutex mutex;
	std::unordered_set<btcb::block_hash> pending;
	/** This is the last block popped off the confirmation height pending collection */
	btcb::block_hash current_hash{ 0 };
	friend class confirmation_height_processor;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (pending_confirmation_height &, const std::string &);

class confirmation_height_processor final
{
public:
	confirmation_height_processor (pending_confirmation_height &, btcb::block_store &, btcb::stat &, btcb::active_transactions &, btcb::block_hash const &, btcb::logger_mt &);
	~confirmation_height_processor ();
	void add (btcb::block_hash const &);
	void stop ();
	bool is_processing_block (btcb::block_hash const &);

	/** The maximum amount of accounts to iterate over while writing */
	static uint64_t constexpr batch_write_size = 2048;

	/** The maximum number of blocks to be read in while iterating over a long account chain */
	static uint64_t constexpr batch_read_size = 4096;

private:
	class conf_height_details final
	{
	public:
		conf_height_details (btcb::account const &, btcb::block_hash const &, uint64_t, uint64_t);

		btcb::account account;
		btcb::block_hash hash;
		uint64_t height;
		uint64_t num_blocks_confirmed;
	};

	class receive_source_pair final
	{
	public:
		receive_source_pair (conf_height_details const &, const btcb::block_hash &);

		conf_height_details receive_details;
		btcb::block_hash source_hash;
	};

	std::condition_variable condition;
	btcb::pending_confirmation_height & pending_confirmations;
	std::atomic<bool> stopped{ false };
	btcb::block_store & store;
	btcb::stat & stats;
	btcb::active_transactions & active;
	btcb::block_hash const & epoch_link;
	btcb::logger_mt & logger;
	std::atomic<uint64_t> receive_source_pairs_size{ 0 };
	std::vector<receive_source_pair> receive_source_pairs;
	std::thread thread;

	void run ();
	void add_confirmation_height (btcb::block_hash const &);
	void collect_unconfirmed_receive_and_sources_for_account (uint64_t, uint64_t, btcb::block_hash const &, btcb::account const &, btcb::read_transaction const &);
	bool write_pending (std::deque<conf_height_details> &, int64_t);

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (confirmation_height_processor &, const std::string &);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (confirmation_height_processor &, const std::string &);
}
