#pragma once

#include <boost/filesystem.hpp>
#include <boost/optional.hpp>
#include <lmdb/libraries/liblmdb/lmdb.h>

#include <btcb/lib/config.hpp>
#include <btcb/lib/logger_mt.hpp>
#include <btcb/lib/numbers.hpp>
#include <btcb/node/diagnosticsconfig.hpp>
#include <btcb/node/lmdb_txn_tracker.hpp>
#include <btcb/secure/blockstore.hpp>
#include <btcb/secure/common.hpp>

#include <thread>

namespace btcb
{
class mdb_env;
class account_info_v13;

class mdb_txn_callbacks
{
public:
	// clang-format off
	std::function<void (const btcb::transaction_impl *)> txn_start{ [] (const btcb::transaction_impl *) {} };
	std::function<void (const btcb::transaction_impl *)> txn_end{ [] (const btcb::transaction_impl *) {} };
	// clang-format on
};

class read_mdb_txn final : public read_transaction_impl
{
public:
	read_mdb_txn (btcb::mdb_env const &, mdb_txn_callbacks mdb_txn_callbacks);
	~read_mdb_txn ();
	void reset () const override;
	void renew () const override;
	void * get_handle () const override;
	MDB_txn * handle;
	mdb_txn_callbacks txn_callbacks;
};

class write_mdb_txn final : public write_transaction_impl
{
public:
	write_mdb_txn (btcb::mdb_env const &, mdb_txn_callbacks mdb_txn_callbacks);
	~write_mdb_txn ();
	void commit () const override;
	void renew () override;
	void * get_handle () const override;
	MDB_txn * handle;
	btcb::mdb_env const & env;
	mdb_txn_callbacks txn_callbacks;
};
/**
 * RAII wrapper for MDB_env
 */
class mdb_env
{
public:
	mdb_env (bool &, boost::filesystem::path const &, int max_dbs = 128, size_t map_size = 128ULL * 1024 * 1024 * 1024);
	~mdb_env ();
	operator MDB_env * () const;
	// clang-format off
	btcb::read_transaction tx_begin_read (mdb_txn_callbacks txn_callbacks = mdb_txn_callbacks{}) const;
	btcb::write_transaction tx_begin_write (mdb_txn_callbacks txn_callbacks = mdb_txn_callbacks{}) const;
	MDB_txn * tx (btcb::transaction const & transaction_a) const;
	// clang-format on
	MDB_env * environment;
};

/**
 * Encapsulates MDB_val and provides uint256_union conversion of the data.
 */
class mdb_val
{
public:
	mdb_val (btcb::epoch = btcb::epoch::unspecified);
	mdb_val (btcb::account_info const &);
	mdb_val (btcb::account_info_v13 const &);
	mdb_val (btcb::block_info const &);
	mdb_val (MDB_val const &, btcb::epoch = btcb::epoch::unspecified);
	mdb_val (btcb::pending_info const &);
	mdb_val (btcb::pending_key const &);
	mdb_val (btcb::unchecked_info const &);
	mdb_val (size_t, void *);
	mdb_val (btcb::uint128_union const &);
	mdb_val (btcb::uint256_union const &);
	mdb_val (btcb::endpoint_key const &);
	mdb_val (std::shared_ptr<btcb::block> const &);
	mdb_val (std::shared_ptr<btcb::vote> const &);
	mdb_val (uint64_t);
	void * data () const;
	size_t size () const;
	explicit operator btcb::account_info () const;
	explicit operator btcb::account_info_v13 () const;
	explicit operator btcb::block_info () const;
	explicit operator btcb::pending_info () const;
	explicit operator btcb::pending_key () const;
	explicit operator btcb::unchecked_info () const;
	explicit operator btcb::uint128_union () const;
	explicit operator btcb::uint256_union () const;
	explicit operator std::array<char, 64> () const;
	explicit operator btcb::endpoint_key () const;
	explicit operator btcb::no_value () const;
	explicit operator std::shared_ptr<btcb::block> () const;
	explicit operator std::shared_ptr<btcb::send_block> () const;
	explicit operator std::shared_ptr<btcb::receive_block> () const;
	explicit operator std::shared_ptr<btcb::open_block> () const;
	explicit operator std::shared_ptr<btcb::change_block> () const;
	explicit operator std::shared_ptr<btcb::state_block> () const;
	explicit operator std::shared_ptr<btcb::vote> () const;
	explicit operator uint64_t () const;
	operator MDB_val * () const;
	operator MDB_val const & () const;
	MDB_val value;
	std::shared_ptr<std::vector<uint8_t>> buffer;
	btcb::epoch epoch{ btcb::epoch::unspecified };
};
class block_store;

template <typename T, typename U>
class mdb_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_iterator (btcb::transaction const & transaction_a, MDB_dbi db_a, btcb::epoch = btcb::epoch::unspecified);
	mdb_iterator (std::nullptr_t, btcb::epoch = btcb::epoch::unspecified);
	mdb_iterator (btcb::transaction const & transaction_a, MDB_dbi db_a, MDB_val const & val_a, btcb::epoch = btcb::epoch::unspecified);
	mdb_iterator (btcb::mdb_iterator<T, U> && other_a);
	mdb_iterator (btcb::mdb_iterator<T, U> const &) = delete;
	~mdb_iterator ();
	btcb::store_iterator_impl<T, U> & operator++ () override;
	std::pair<btcb::mdb_val, btcb::mdb_val> * operator-> ();
	bool operator== (btcb::store_iterator_impl<T, U> const & other_a) const override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	btcb::mdb_iterator<T, U> & operator= (btcb::mdb_iterator<T, U> && other_a);
	btcb::store_iterator_impl<T, U> & operator= (btcb::store_iterator_impl<T, U> const &) = delete;
	MDB_cursor * cursor;
	std::pair<btcb::mdb_val, btcb::mdb_val> current;

private:
	MDB_txn * tx (btcb::transaction const &) const;
};

/**
 * Iterates the key/value pairs of two stores merged together
 */
template <typename T, typename U>
class mdb_merge_iterator : public store_iterator_impl<T, U>
{
public:
	mdb_merge_iterator (btcb::transaction const &, MDB_dbi, MDB_dbi);
	mdb_merge_iterator (std::nullptr_t);
	mdb_merge_iterator (btcb::transaction const &, MDB_dbi, MDB_dbi, MDB_val const &);
	mdb_merge_iterator (btcb::mdb_merge_iterator<T, U> &&);
	mdb_merge_iterator (btcb::mdb_merge_iterator<T, U> const &) = delete;
	~mdb_merge_iterator ();
	btcb::store_iterator_impl<T, U> & operator++ () override;
	std::pair<btcb::mdb_val, btcb::mdb_val> * operator-> ();
	bool operator== (btcb::store_iterator_impl<T, U> const &) const override;
	bool is_end_sentinal () const override;
	void fill (std::pair<T, U> &) const override;
	void clear ();
	btcb::mdb_merge_iterator<T, U> & operator= (btcb::mdb_merge_iterator<T, U> &&) = default;
	btcb::mdb_merge_iterator<T, U> & operator= (btcb::mdb_merge_iterator<T, U> const &) = delete;

private:
	btcb::mdb_iterator<T, U> & least_iterator () const;
	std::unique_ptr<btcb::mdb_iterator<T, U>> impl1;
	std::unique_ptr<btcb::mdb_iterator<T, U>> impl2;
};

class logging_mt;
/**
 * mdb implementation of the block store
 */
class mdb_store : public block_store
{
	friend class btcb::block_predecessor_set;

public:
	mdb_store (bool &, btcb::logger_mt &, boost::filesystem::path const &, btcb::txn_tracking_config const & txn_tracking_config_a = btcb::txn_tracking_config{}, std::chrono::milliseconds block_processor_batch_max_time_a = std::chrono::milliseconds (5000), int lmdb_max_dbs = 128, bool drop_unchecked = false, size_t batch_size = 512);
	btcb::write_transaction tx_begin_write () override;
	btcb::read_transaction tx_begin_read () override;

	void initialize (btcb::transaction const &, btcb::genesis const &) override;
	void block_put (btcb::transaction const &, btcb::block_hash const &, btcb::block const &, btcb::block_sideband const &, btcb::epoch version = btcb::epoch::epoch_0) override;
	btcb::block_hash block_successor (btcb::transaction const &, btcb::block_hash const &) const override;
	void block_successor_clear (btcb::transaction const &, btcb::block_hash const &) override;
	std::shared_ptr<btcb::block> block_get (btcb::transaction const &, btcb::block_hash const &, btcb::block_sideband * = nullptr) const override;
	std::shared_ptr<btcb::block> block_random (btcb::transaction const &) override;
	void block_del (btcb::transaction const &, btcb::block_hash const &) override;
	bool block_exists (btcb::transaction const &, btcb::block_hash const &) override;
	bool block_exists (btcb::transaction const &, btcb::block_type, btcb::block_hash const &) override;
	btcb::block_counts block_count (btcb::transaction const &) override;
	bool root_exists (btcb::transaction const &, btcb::uint256_union const &) override;
	bool source_exists (btcb::transaction const &, btcb::block_hash const &) override;
	btcb::account block_account (btcb::transaction const &, btcb::block_hash const &) const override;

	void frontier_put (btcb::transaction const &, btcb::block_hash const &, btcb::account const &) override;
	btcb::account frontier_get (btcb::transaction const &, btcb::block_hash const &) const override;
	void frontier_del (btcb::transaction const &, btcb::block_hash const &) override;

	void account_put (btcb::transaction const &, btcb::account const &, btcb::account_info const &) override;
	bool account_get (btcb::transaction const &, btcb::account const &, btcb::account_info &) override;
	void account_del (btcb::transaction const &, btcb::account const &) override;
	bool account_exists (btcb::transaction const &, btcb::account const &) override;
	size_t account_count (btcb::transaction const &) override;
	void confirmation_height_clear (btcb::transaction const &, btcb::account const & account, btcb::account_info const & account_info) override;
	void confirmation_height_clear (btcb::transaction const &) override;
	btcb::store_iterator<btcb::account, btcb::account_info> latest_v0_begin (btcb::transaction const &, btcb::account const &) override;
	btcb::store_iterator<btcb::account, btcb::account_info> latest_v0_begin (btcb::transaction const &) override;
	btcb::store_iterator<btcb::account, btcb::account_info> latest_v0_end () override;
	btcb::store_iterator<btcb::account, btcb::account_info> latest_v1_begin (btcb::transaction const &, btcb::account const &) override;
	btcb::store_iterator<btcb::account, btcb::account_info> latest_v1_begin (btcb::transaction const &) override;
	btcb::store_iterator<btcb::account, btcb::account_info> latest_v1_end () override;
	btcb::store_iterator<btcb::account, btcb::account_info> latest_begin (btcb::transaction const &, btcb::account const &) override;
	btcb::store_iterator<btcb::account, btcb::account_info> latest_begin (btcb::transaction const &) override;
	btcb::store_iterator<btcb::account, btcb::account_info> latest_end () override;

	void pending_put (btcb::transaction const &, btcb::pending_key const &, btcb::pending_info const &) override;
	void pending_del (btcb::transaction const &, btcb::pending_key const &) override;
	bool pending_get (btcb::transaction const &, btcb::pending_key const &, btcb::pending_info &) override;
	bool pending_exists (btcb::transaction const &, btcb::pending_key const &) override;
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> pending_v0_begin (btcb::transaction const &, btcb::pending_key const &) override;
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> pending_v0_begin (btcb::transaction const &) override;
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> pending_v0_end () override;
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> pending_v1_begin (btcb::transaction const &, btcb::pending_key const &) override;
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> pending_v1_begin (btcb::transaction const &) override;
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> pending_v1_end () override;
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> pending_begin (btcb::transaction const &, btcb::pending_key const &) override;
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> pending_begin (btcb::transaction const &) override;
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> pending_end () override;

	bool block_info_get (btcb::transaction const &, btcb::block_hash const &, btcb::block_info &) const override;
	btcb::uint128_t block_balance (btcb::transaction const &, btcb::block_hash const &) override;
	btcb::epoch block_version (btcb::transaction const &, btcb::block_hash const &) override;

	btcb::uint128_t representation_get (btcb::transaction const &, btcb::account const &) override;
	void representation_put (btcb::transaction const &, btcb::account const &, btcb::uint128_t const &) override;
	void representation_add (btcb::transaction const &, btcb::account const &, btcb::uint128_t const &) override;
	btcb::store_iterator<btcb::account, btcb::uint128_union> representation_begin (btcb::transaction const &) override;
	btcb::store_iterator<btcb::account, btcb::uint128_union> representation_end () override;

	void unchecked_clear (btcb::transaction const &) override;
	void unchecked_put (btcb::transaction const &, btcb::unchecked_key const &, btcb::unchecked_info const &) override;
	void unchecked_put (btcb::transaction const &, btcb::block_hash const &, std::shared_ptr<btcb::block> const &) override;
	std::vector<btcb::unchecked_info> unchecked_get (btcb::transaction const &, btcb::block_hash const &) override;
	void unchecked_del (btcb::transaction const &, btcb::unchecked_key const &) override;
	btcb::store_iterator<btcb::unchecked_key, btcb::unchecked_info> unchecked_begin (btcb::transaction const &) override;
	btcb::store_iterator<btcb::unchecked_key, btcb::unchecked_info> unchecked_begin (btcb::transaction const &, btcb::unchecked_key const &) override;
	btcb::store_iterator<btcb::unchecked_key, btcb::unchecked_info> unchecked_end () override;
	size_t unchecked_count (btcb::transaction const &) override;

	// Return latest vote for an account from store
	std::shared_ptr<btcb::vote> vote_get (btcb::transaction const &, btcb::account const &) override;
	// Populate vote with the next sequence number
	std::shared_ptr<btcb::vote> vote_generate (btcb::transaction const &, btcb::account const &, btcb::raw_key const &, std::shared_ptr<btcb::block>) override;
	std::shared_ptr<btcb::vote> vote_generate (btcb::transaction const &, btcb::account const &, btcb::raw_key const &, std::vector<btcb::block_hash>) override;
	// Return either vote or the stored vote with a higher sequence number
	std::shared_ptr<btcb::vote> vote_max (btcb::transaction const &, std::shared_ptr<btcb::vote>) override;
	// Return latest vote for an account considering the vote cache
	std::shared_ptr<btcb::vote> vote_current (btcb::transaction const &, btcb::account const &) override;
	void flush (btcb::transaction const &) override;
	btcb::store_iterator<btcb::account, std::shared_ptr<btcb::vote>> vote_begin (btcb::transaction const &) override;
	btcb::store_iterator<btcb::account, std::shared_ptr<btcb::vote>> vote_end () override;

	void online_weight_put (btcb::transaction const &, uint64_t, btcb::amount const &) override;
	void online_weight_del (btcb::transaction const &, uint64_t) override;
	btcb::store_iterator<uint64_t, btcb::amount> online_weight_begin (btcb::transaction const &) override;
	btcb::store_iterator<uint64_t, btcb::amount> online_weight_end () override;
	size_t online_weight_count (btcb::transaction const &) const override;
	void online_weight_clear (btcb::transaction const &) override;

	std::mutex cache_mutex;
	std::unordered_map<btcb::account, std::shared_ptr<btcb::vote>> vote_cache_l1;
	std::unordered_map<btcb::account, std::shared_ptr<btcb::vote>> vote_cache_l2;

	void version_put (btcb::transaction const &, int) override;
	int version_get (btcb::transaction const &) const override;

	void peer_put (btcb::transaction const & transaction_a, btcb::endpoint_key const & endpoint_a) override;
	bool peer_exists (btcb::transaction const & transaction_a, btcb::endpoint_key const & endpoint_a) const override;
	void peer_del (btcb::transaction const & transaction_a, btcb::endpoint_key const & endpoint_a) override;
	size_t peer_count (btcb::transaction const & transaction_a) const override;
	void peer_clear (btcb::transaction const & transaction_a) override;

	btcb::store_iterator<btcb::endpoint_key, btcb::no_value> peers_begin (btcb::transaction const & transaction_a) override;
	btcb::store_iterator<btcb::endpoint_key, btcb::no_value> peers_end () override;

	uint64_t block_account_height (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a) const override;

	bool full_sideband (btcb::transaction const &) const;
	MDB_dbi get_account_db (btcb::epoch epoch_a) const;
	size_t block_successor_offset (btcb::transaction const &, MDB_val, btcb::block_type) const;
	void serialize_mdb_tracker (boost::property_tree::ptree &, std::chrono::milliseconds, std::chrono::milliseconds) override;

	btcb::logger_mt & logger;

	btcb::mdb_env env;

	/**
	 * Maps head block to owning account
	 * btcb::block_hash -> btcb::account
	 */
	MDB_dbi frontiers{ 0 };

	/**
	 * Maps account v1 to account information, head, rep, open, balance, timestamp and block count.
	 * btcb::account -> btcb::block_hash, btcb::block_hash, btcb::block_hash, btcb::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v0{ 0 };

	/**
	 * Maps account v0 to account information, head, rep, open, balance, timestamp and block count.
	 * btcb::account -> btcb::block_hash, btcb::block_hash, btcb::block_hash, btcb::amount, uint64_t, uint64_t
	 */
	MDB_dbi accounts_v1{ 0 };

	/**
	 * Maps block hash to send block.
	 * btcb::block_hash -> btcb::send_block
	 */
	MDB_dbi send_blocks{ 0 };

	/**
	 * Maps block hash to receive block.
	 * btcb::block_hash -> btcb::receive_block
	 */
	MDB_dbi receive_blocks{ 0 };

	/**
	 * Maps block hash to open block.
	 * btcb::block_hash -> btcb::open_block
	 */
	MDB_dbi open_blocks{ 0 };

	/**
	 * Maps block hash to change block.
	 * btcb::block_hash -> btcb::change_block
	 */
	MDB_dbi change_blocks{ 0 };

	/**
	 * Maps block hash to v0 state block.
	 * btcb::block_hash -> btcb::state_block
	 */
	MDB_dbi state_blocks_v0{ 0 };

	/**
	 * Maps block hash to v1 state block.
	 * btcb::block_hash -> btcb::state_block
	 */
	MDB_dbi state_blocks_v1{ 0 };

	/**
	 * Maps min_version 0 (destination account, pending block) to (source account, amount).
	 * btcb::account, btcb::block_hash -> btcb::account, btcb::amount
	 */
	MDB_dbi pending_v0{ 0 };

	/**
	 * Maps min_version 1 (destination account, pending block) to (source account, amount).
	 * btcb::account, btcb::block_hash -> btcb::account, btcb::amount
	 */
	MDB_dbi pending_v1{ 0 };

	/**
	 * Maps block hash to account and balance.
	 * block_hash -> btcb::account, btcb::amount
	 */
	MDB_dbi blocks_info{ 0 };

	/**
	 * Representative weights.
	 * btcb::account -> btcb::uint128_t
	 */
	MDB_dbi representation{ 0 };

	/**
	 * Unchecked bootstrap blocks info.
	 * btcb::block_hash -> btcb::unchecked_info
	 */
	MDB_dbi unchecked{ 0 };

	/**
	 * Highest vote observed for account.
	 * btcb::account -> uint64_t
	 */
	MDB_dbi vote{ 0 };

	/**
	 * Samples of online vote weight
	 * uint64_t -> btcb::amount
	 */
	MDB_dbi online_weight{ 0 };

	/**
	 * Meta information about block store, such as versions.
	 * btcb::uint256_union (arbitrary key) -> blob
	 */
	MDB_dbi meta{ 0 };

	/*
	 * Endpoints for peers
	 * btcb::endpoint_key -> no_value
	*/
	MDB_dbi peers{ 0 };

private:
	btcb::network_params network_params;
	bool entry_has_sideband (MDB_val, btcb::block_type) const;
	btcb::account block_account_computed (btcb::transaction const &, btcb::block_hash const &) const;
	btcb::uint128_t block_balance_computed (btcb::transaction const &, btcb::block_hash const &) const;
	MDB_dbi block_database (btcb::block_type, btcb::epoch);
	template <typename T>
	std::shared_ptr<btcb::block> block_random (btcb::transaction const &, MDB_dbi);
	MDB_val block_raw_get (btcb::transaction const &, btcb::block_hash const &, btcb::block_type &) const;
	boost::optional<MDB_val> block_raw_get_by_type (btcb::transaction const &, btcb::block_hash const &, btcb::block_type &) const;
	void block_raw_put (btcb::transaction const &, MDB_dbi, btcb::block_hash const &, MDB_val);
	void clear (MDB_dbi);
	bool do_upgrades (btcb::write_transaction &, size_t);
	void upgrade_v1_to_v2 (btcb::transaction const &);
	void upgrade_v2_to_v3 (btcb::transaction const &);
	void upgrade_v3_to_v4 (btcb::transaction const &);
	void upgrade_v4_to_v5 (btcb::transaction const &);
	void upgrade_v5_to_v6 (btcb::transaction const &);
	void upgrade_v6_to_v7 (btcb::transaction const &);
	void upgrade_v7_to_v8 (btcb::transaction const &);
	void upgrade_v8_to_v9 (btcb::transaction const &);
	void upgrade_v9_to_v10 (btcb::transaction const &);
	void upgrade_v10_to_v11 (btcb::transaction const &);
	void upgrade_v11_to_v12 (btcb::transaction const &);
	void upgrade_v12_to_v13 (btcb::write_transaction &, size_t);
	void upgrade_v13_to_v14 (btcb::transaction const &);
	MDB_dbi get_pending_db (btcb::epoch epoch_a) const;
	void open_databases (bool &, btcb::transaction const &, unsigned);
	btcb::mdb_txn_tracker mdb_txn_tracker;
	btcb::mdb_txn_callbacks create_txn_callbacks ();
	bool txn_tracking_enabled;
	static int constexpr version{ 14 };
};
class wallet_value
{
public:
	wallet_value () = default;
	wallet_value (btcb::mdb_val const &);
	wallet_value (btcb::uint256_union const &, uint64_t);
	btcb::mdb_val val () const;
	btcb::private_key key;
	uint64_t work;
};
}
