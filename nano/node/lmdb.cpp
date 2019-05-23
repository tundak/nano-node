#include <btcb/node/lmdb.hpp>

#include <btcb/crypto_lib/random_pool.hpp>
#include <btcb/lib/utility.hpp>
#include <btcb/node/common.hpp>
#include <btcb/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/polymorphic_cast.hpp>

#include <queue>

btcb::mdb_env::mdb_env (bool & error_a, boost::filesystem::path const & path_a, int max_dbs, size_t map_size_a)
{
	boost::system::error_code error_mkdir, error_chmod;
	if (path_a.has_parent_path ())
	{
		boost::filesystem::create_directories (path_a.parent_path (), error_mkdir);
		btcb::set_secure_perm_directory (path_a.parent_path (), error_chmod);
		if (!error_mkdir)
		{
			auto status1 (mdb_env_create (&environment));
			release_assert (status1 == 0);
			auto status2 (mdb_env_set_maxdbs (environment, max_dbs));
			release_assert (status2 == 0);
			auto status3 (mdb_env_set_mapsize (environment, map_size_a));
			release_assert (status3 == 0);
			// It seems if there's ever more threads than mdb_env_set_maxreaders has read slots available, we get failures on transaction creation unless MDB_NOTLS is specified
			// This can happen if something like 256 io_threads are specified in the node config
			// MDB_NORDAHEAD will allow platforms that support it to load the DB in memory as needed.
			auto status4 (mdb_env_open (environment, path_a.string ().c_str (), MDB_NOSUBDIR | MDB_NOTLS | MDB_NORDAHEAD, 00600));
			if (status4 != 0)
			{
				std::cerr << "Could not open lmdb environment: " << status4;
				char * error_str (mdb_strerror (status4));
				if (error_str)
				{
					std::cerr << ", " << error_str;
				}
				std::cerr << std::endl;
			}
			release_assert (status4 == 0);
			error_a = status4 != 0;
		}
		else
		{
			error_a = true;
			environment = nullptr;
		}
	}
	else
	{
		error_a = true;
		environment = nullptr;
	}
}

btcb::mdb_env::~mdb_env ()
{
	if (environment != nullptr)
	{
		mdb_env_close (environment);
	}
}

btcb::mdb_env::operator MDB_env * () const
{
	return environment;
}

btcb::read_transaction btcb::mdb_env::tx_begin_read (mdb_txn_callbacks mdb_txn_callbacks) const
{
	return btcb::read_transaction{ std::make_unique<btcb::read_mdb_txn> (*this, mdb_txn_callbacks) };
}

btcb::write_transaction btcb::mdb_env::tx_begin_write (mdb_txn_callbacks mdb_txn_callbacks) const
{
	return btcb::write_transaction{ std::make_unique<btcb::write_mdb_txn> (*this, mdb_txn_callbacks) };
}

MDB_txn * btcb::mdb_env::tx (btcb::transaction const & transaction_a) const
{
	return static_cast<MDB_txn *> (transaction_a.get_handle ());
}

btcb::read_mdb_txn::read_mdb_txn (btcb::mdb_env const & environment_a, btcb::mdb_txn_callbacks txn_callbacks_a) :
txn_callbacks (txn_callbacks_a)
{
	auto status (mdb_txn_begin (environment_a, nullptr, MDB_RDONLY, &handle));
	release_assert (status == 0);
	txn_callbacks.txn_start (this);
}

btcb::read_mdb_txn::~read_mdb_txn ()
{
	// This uses commit rather than abort, as it is needed when opening databases with a read only transaction
	auto status (mdb_txn_commit (handle));
	release_assert (status == MDB_SUCCESS);
	txn_callbacks.txn_end (this);
}

void btcb::read_mdb_txn::reset () const
{
	mdb_txn_reset (handle);
	txn_callbacks.txn_end (this);
}

void btcb::read_mdb_txn::renew () const
{
	auto status (mdb_txn_renew (handle));
	release_assert (status == 0);
	txn_callbacks.txn_start (this);
}

void * btcb::read_mdb_txn::get_handle () const
{
	return handle;
}

btcb::write_mdb_txn::write_mdb_txn (btcb::mdb_env const & environment_a, btcb::mdb_txn_callbacks txn_callbacks_a) :
env (environment_a),
txn_callbacks (txn_callbacks_a)
{
	renew ();
}

btcb::write_mdb_txn::~write_mdb_txn ()
{
	commit ();
}

void btcb::write_mdb_txn::commit () const
{
	auto status (mdb_txn_commit (handle));
	release_assert (status == MDB_SUCCESS);
	txn_callbacks.txn_end (this);
}

void btcb::write_mdb_txn::renew ()
{
	auto status (mdb_txn_begin (env, nullptr, 0, &handle));
	release_assert (status == MDB_SUCCESS);
	txn_callbacks.txn_start (this);
}

void * btcb::write_mdb_txn::get_handle () const
{
	return handle;
}

btcb::mdb_val::mdb_val (btcb::epoch epoch_a) :
value ({ 0, nullptr }),
epoch (epoch_a)
{
}

btcb::mdb_val::mdb_val (MDB_val const & value_a, btcb::epoch epoch_a) :
value (value_a),
epoch (epoch_a)
{
}

btcb::mdb_val::mdb_val (size_t size_a, void * data_a) :
value ({ size_a, data_a })
{
}

btcb::mdb_val::mdb_val (btcb::uint128_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<btcb::uint128_union *> (&val_a))
{
}

btcb::mdb_val::mdb_val (btcb::uint256_union const & val_a) :
mdb_val (sizeof (val_a), const_cast<btcb::uint256_union *> (&val_a))
{
}

btcb::mdb_val::mdb_val (btcb::account_info const & val_a) :
mdb_val (val_a.db_size (), const_cast<btcb::account_info *> (&val_a))
{
}

btcb::mdb_val::mdb_val (btcb::account_info_v13 const & val_a) :
mdb_val (val_a.db_size (), const_cast<btcb::account_info_v13 *> (&val_a))
{
}

btcb::mdb_val::mdb_val (btcb::pending_info const & val_a) :
mdb_val (sizeof (val_a.source) + sizeof (val_a.amount), const_cast<btcb::pending_info *> (&val_a))
{
	static_assert (std::is_standard_layout<btcb::pending_info>::value, "Standard layout is required");
}

btcb::mdb_val::mdb_val (btcb::pending_key const & val_a) :
mdb_val (sizeof (val_a), const_cast<btcb::pending_key *> (&val_a))
{
	static_assert (std::is_standard_layout<btcb::pending_key>::value, "Standard layout is required");
}

btcb::mdb_val::mdb_val (btcb::unchecked_info const & val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		btcb::vectorstream stream (*buffer);
		val_a.serialize (stream);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

btcb::mdb_val::mdb_val (btcb::block_info const & val_a) :
mdb_val (sizeof (val_a), const_cast<btcb::block_info *> (&val_a))
{
	static_assert (std::is_standard_layout<btcb::block_info>::value, "Standard layout is required");
}

btcb::mdb_val::mdb_val (btcb::endpoint_key const & val_a) :
mdb_val (sizeof (val_a), const_cast<btcb::endpoint_key *> (&val_a))
{
	static_assert (std::is_standard_layout<btcb::endpoint_key>::value, "Standard layout is required");
}

btcb::mdb_val::mdb_val (std::shared_ptr<btcb::block> const & val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		btcb::vectorstream stream (*buffer);
		btcb::serialize_block (stream, *val_a);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

btcb::mdb_val::mdb_val (uint64_t val_a) :
buffer (std::make_shared<std::vector<uint8_t>> ())
{
	{
		boost::endian::native_to_big_inplace (val_a);
		btcb::vectorstream stream (*buffer);
		btcb::write (stream, val_a);
	}
	value = { buffer->size (), const_cast<uint8_t *> (buffer->data ()) };
}

void * btcb::mdb_val::data () const
{
	return value.mv_data;
}

size_t btcb::mdb_val::size () const
{
	return value.mv_size;
}

btcb::mdb_val::operator btcb::account_info () const
{
	btcb::account_info result;
	result.epoch = epoch;
	assert (value.mv_size == result.db_size ());
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

btcb::mdb_val::operator btcb::account_info_v13 () const
{
	btcb::account_info_v13 result;
	result.epoch = epoch;
	assert (value.mv_size == result.db_size ());
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + result.db_size (), reinterpret_cast<uint8_t *> (&result));
	return result;
}

btcb::mdb_val::operator btcb::block_info () const
{
	btcb::block_info result;
	assert (value.mv_size == sizeof (result));
	static_assert (sizeof (btcb::block_info::account) + sizeof (btcb::block_info::balance) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

btcb::mdb_val::operator btcb::pending_info () const
{
	btcb::pending_info result;
	result.epoch = epoch;
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (btcb::pending_info::source) + sizeof (btcb::pending_info::amount), reinterpret_cast<uint8_t *> (&result));
	return result;
}

btcb::mdb_val::operator btcb::pending_key () const
{
	btcb::pending_key result;
	assert (value.mv_size == sizeof (result));
	static_assert (sizeof (btcb::pending_key::account) + sizeof (btcb::pending_key::hash) == sizeof (result), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

btcb::mdb_val::operator btcb::unchecked_info () const
{
	btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	btcb::unchecked_info result;
	bool error (result.deserialize (stream));
	assert (!error);
	return result;
}

btcb::mdb_val::operator btcb::uint128_union () const
{
	btcb::uint128_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

btcb::mdb_val::operator btcb::uint256_union () const
{
	btcb::uint256_union result;
	assert (size () == sizeof (result));
	std::copy (reinterpret_cast<uint8_t const *> (data ()), reinterpret_cast<uint8_t const *> (data ()) + sizeof (result), result.bytes.data ());
	return result;
}

btcb::mdb_val::operator std::array<char, 64> () const
{
	btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	std::array<char, 64> result;
	auto error = btcb::try_read (stream, result);
	assert (!error);
	return result;
}

btcb::mdb_val::operator btcb::endpoint_key () const
{
	btcb::endpoint_key result;
	std::copy (reinterpret_cast<uint8_t const *> (value.mv_data), reinterpret_cast<uint8_t const *> (value.mv_data) + sizeof (result), reinterpret_cast<uint8_t *> (&result));
	return result;
}

btcb::mdb_val::operator btcb::no_value () const
{
	return no_value::dummy;
}

btcb::mdb_val::operator std::shared_ptr<btcb::block> () const
{
	btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	std::shared_ptr<btcb::block> result (btcb::deserialize_block (stream));
	return result;
}

btcb::mdb_val::operator std::shared_ptr<btcb::send_block> () const
{
	btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<btcb::send_block> result (std::make_shared<btcb::send_block> (error, stream));
	assert (!error);
	return result;
}

btcb::mdb_val::operator std::shared_ptr<btcb::receive_block> () const
{
	btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<btcb::receive_block> result (std::make_shared<btcb::receive_block> (error, stream));
	assert (!error);
	return result;
}

btcb::mdb_val::operator std::shared_ptr<btcb::open_block> () const
{
	btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<btcb::open_block> result (std::make_shared<btcb::open_block> (error, stream));
	assert (!error);
	return result;
}

btcb::mdb_val::operator std::shared_ptr<btcb::change_block> () const
{
	btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<btcb::change_block> result (std::make_shared<btcb::change_block> (error, stream));
	assert (!error);
	return result;
}

btcb::mdb_val::operator std::shared_ptr<btcb::state_block> () const
{
	btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<btcb::state_block> result (std::make_shared<btcb::state_block> (error, stream));
	assert (!error);
	return result;
}

btcb::mdb_val::operator std::shared_ptr<btcb::vote> () const
{
	btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (false);
	std::shared_ptr<btcb::vote> result (std::make_shared<btcb::vote> (error, stream));
	assert (!error);
	return result;
}

btcb::mdb_val::operator uint64_t () const
{
	uint64_t result;
	btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
	auto error (btcb::try_read (stream, result));
	assert (!error);
	boost::endian::big_to_native_inplace (result);
	return result;
}

btcb::mdb_val::operator MDB_val * () const
{
	// Allow passing a temporary to a non-c++ function which doesn't have constness
	return const_cast<MDB_val *> (&value);
}

btcb::mdb_val::operator MDB_val const & () const
{
	return value;
}

namespace btcb
{
/**
 * Fill in our predecessors
 */
class block_predecessor_set : public btcb::block_visitor
{
public:
	block_predecessor_set (btcb::transaction const & transaction_a, btcb::mdb_store & store_a) :
	transaction (transaction_a),
	store (store_a)
	{
	}
	virtual ~block_predecessor_set () = default;
	void fill_value (btcb::block const & block_a)
	{
		auto hash (block_a.hash ());
		btcb::block_type type;
		auto value (store.block_raw_get (transaction, block_a.previous (), type));
		auto version (store.block_version (transaction, block_a.previous ()));
		assert (value.mv_size != 0);
		std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
		std::copy (hash.bytes.begin (), hash.bytes.end (), data.begin () + store.block_successor_offset (transaction, value, type));
		store.block_raw_put (transaction, store.block_database (type, version), block_a.previous (), btcb::mdb_val (data.size (), data.data ()));
	}
	void send_block (btcb::send_block const & block_a) override
	{
		fill_value (block_a);
	}
	void receive_block (btcb::receive_block const & block_a) override
	{
		fill_value (block_a);
	}
	void open_block (btcb::open_block const & block_a) override
	{
		// Open blocks don't have a predecessor
	}
	void change_block (btcb::change_block const & block_a) override
	{
		fill_value (block_a);
	}
	void state_block (btcb::state_block const & block_a) override
	{
		if (!block_a.previous ().is_zero ())
		{
			fill_value (block_a);
		}
	}
	btcb::transaction const & transaction;
	btcb::mdb_store & store;
};
}

template <typename T, typename U>
btcb::mdb_iterator<T, U>::mdb_iterator (btcb::transaction const & transaction_a, MDB_dbi db_a, btcb::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
	auto status (mdb_cursor_open (tx (transaction_a), db_a, &cursor));
	release_assert (status == 0);
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_FIRST));
	release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
	}
	else
	{
		clear ();
	}
}

template <typename T, typename U>
btcb::mdb_iterator<T, U>::mdb_iterator (std::nullptr_t, btcb::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
}

template <typename T, typename U>
btcb::mdb_iterator<T, U>::mdb_iterator (btcb::transaction const & transaction_a, MDB_dbi db_a, MDB_val const & val_a, btcb::epoch epoch_a) :
cursor (nullptr)
{
	current.first.epoch = epoch_a;
	current.second.epoch = epoch_a;
	auto status (mdb_cursor_open (tx (transaction_a), db_a, &cursor));
	release_assert (status == 0);
	current.first = val_a;
	auto status2 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_SET_RANGE));
	release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
	if (status2 != MDB_NOTFOUND)
	{
		auto status3 (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_GET_CURRENT));
		release_assert (status3 == 0 || status3 == MDB_NOTFOUND);
		if (current.first.size () != sizeof (T))
		{
			clear ();
		}
	}
	else
	{
		clear ();
	}
}

template <typename T, typename U>
btcb::mdb_iterator<T, U>::mdb_iterator (btcb::mdb_iterator<T, U> && other_a)
{
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
}

template <typename T, typename U>
btcb::mdb_iterator<T, U>::~mdb_iterator ()
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
}

template <typename T, typename U>
btcb::store_iterator_impl<T, U> & btcb::mdb_iterator<T, U>::operator++ ()
{
	assert (cursor != nullptr);
	auto status (mdb_cursor_get (cursor, &current.first.value, &current.second.value, MDB_NEXT));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status == MDB_NOTFOUND)
	{
		clear ();
	}
	if (current.first.size () != sizeof (T))
	{
		clear ();
	}
	return *this;
}

template <typename T, typename U>
btcb::mdb_iterator<T, U> & btcb::mdb_iterator<T, U>::operator= (btcb::mdb_iterator<T, U> && other_a)
{
	if (cursor != nullptr)
	{
		mdb_cursor_close (cursor);
	}
	cursor = other_a.cursor;
	other_a.cursor = nullptr;
	current = other_a.current;
	other_a.clear ();
	return *this;
}

template <typename T, typename U>
std::pair<btcb::mdb_val, btcb::mdb_val> * btcb::mdb_iterator<T, U>::operator-> ()
{
	return &current;
}

template <typename T, typename U>
bool btcb::mdb_iterator<T, U>::operator== (btcb::store_iterator_impl<T, U> const & base_a) const
{
	auto const other_a (boost::polymorphic_downcast<btcb::mdb_iterator<T, U> const *> (&base_a));
	auto result (current.first.data () == other_a->current.first.data ());
	assert (!result || (current.first.size () == other_a->current.first.size ()));
	assert (!result || (current.second.data () == other_a->current.second.data ()));
	assert (!result || (current.second.size () == other_a->current.second.size ()));
	return result;
}

template <typename T, typename U>
void btcb::mdb_iterator<T, U>::clear ()
{
	current.first = btcb::mdb_val (current.first.epoch);
	current.second = btcb::mdb_val (current.second.epoch);
	assert (is_end_sentinal ());
}

template <typename T, typename U>
MDB_txn * btcb::mdb_iterator<T, U>::tx (btcb::transaction const & transaction_a) const
{
	return static_cast<MDB_txn *> (transaction_a.get_handle ());
}

template <typename T, typename U>
bool btcb::mdb_iterator<T, U>::is_end_sentinal () const
{
	return current.first.size () == 0;
}

template <typename T, typename U>
void btcb::mdb_iterator<T, U>::fill (std::pair<T, U> & value_a) const
{
	if (current.first.size () != 0)
	{
		value_a.first = static_cast<T> (current.first);
	}
	else
	{
		value_a.first = T ();
	}
	if (current.second.size () != 0)
	{
		value_a.second = static_cast<U> (current.second);
	}
	else
	{
		value_a.second = U ();
	}
}

template <typename T, typename U>
std::pair<btcb::mdb_val, btcb::mdb_val> * btcb::mdb_merge_iterator<T, U>::operator-> ()
{
	return least_iterator ().operator-> ();
}

template <typename T, typename U>
btcb::mdb_merge_iterator<T, U>::mdb_merge_iterator (btcb::transaction const & transaction_a, MDB_dbi db1_a, MDB_dbi db2_a) :
impl1 (std::make_unique<btcb::mdb_iterator<T, U>> (transaction_a, db1_a, btcb::epoch::epoch_0)),
impl2 (std::make_unique<btcb::mdb_iterator<T, U>> (transaction_a, db2_a, btcb::epoch::epoch_1))
{
}

template <typename T, typename U>
btcb::mdb_merge_iterator<T, U>::mdb_merge_iterator (std::nullptr_t) :
impl1 (std::make_unique<btcb::mdb_iterator<T, U>> (nullptr, btcb::epoch::epoch_0)),
impl2 (std::make_unique<btcb::mdb_iterator<T, U>> (nullptr, btcb::epoch::epoch_1))
{
}

template <typename T, typename U>
btcb::mdb_merge_iterator<T, U>::mdb_merge_iterator (btcb::transaction const & transaction_a, MDB_dbi db1_a, MDB_dbi db2_a, MDB_val const & val_a) :
impl1 (std::make_unique<btcb::mdb_iterator<T, U>> (transaction_a, db1_a, val_a, btcb::epoch::epoch_0)),
impl2 (std::make_unique<btcb::mdb_iterator<T, U>> (transaction_a, db2_a, val_a, btcb::epoch::epoch_1))
{
}

template <typename T, typename U>
btcb::mdb_merge_iterator<T, U>::mdb_merge_iterator (btcb::mdb_merge_iterator<T, U> && other_a)
{
	impl1 = std::move (other_a.impl1);
	impl2 = std::move (other_a.impl2);
}

template <typename T, typename U>
btcb::mdb_merge_iterator<T, U>::~mdb_merge_iterator ()
{
}

template <typename T, typename U>
btcb::store_iterator_impl<T, U> & btcb::mdb_merge_iterator<T, U>::operator++ ()
{
	++least_iterator ();
	return *this;
}

template <typename T, typename U>
bool btcb::mdb_merge_iterator<T, U>::is_end_sentinal () const
{
	return least_iterator ().is_end_sentinal ();
}

template <typename T, typename U>
void btcb::mdb_merge_iterator<T, U>::fill (std::pair<T, U> & value_a) const
{
	auto & current (least_iterator ());
	if (current->first.size () != 0)
	{
		value_a.first = static_cast<T> (current->first);
	}
	else
	{
		value_a.first = T ();
	}
	if (current->second.size () != 0)
	{
		value_a.second = static_cast<U> (current->second);
	}
	else
	{
		value_a.second = U ();
	}
}

template <typename T, typename U>
bool btcb::mdb_merge_iterator<T, U>::operator== (btcb::store_iterator_impl<T, U> const & base_a) const
{
	assert ((dynamic_cast<btcb::mdb_merge_iterator<T, U> const *> (&base_a) != nullptr) && "Incompatible iterator comparison");
	auto & other (static_cast<btcb::mdb_merge_iterator<T, U> const &> (base_a));
	return *impl1 == *other.impl1 && *impl2 == *other.impl2;
}

template <typename T, typename U>
btcb::mdb_iterator<T, U> & btcb::mdb_merge_iterator<T, U>::least_iterator () const
{
	btcb::mdb_iterator<T, U> * result;
	if (impl1->is_end_sentinal ())
	{
		result = impl2.get ();
	}
	else if (impl2->is_end_sentinal ())
	{
		result = impl1.get ();
	}
	else
	{
		auto key_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.first, impl2->current.first));

		if (key_cmp < 0)
		{
			result = impl1.get ();
		}
		else if (key_cmp > 0)
		{
			result = impl2.get ();
		}
		else
		{
			auto val_cmp (mdb_cmp (mdb_cursor_txn (impl1->cursor), mdb_cursor_dbi (impl1->cursor), impl1->current.second, impl2->current.second));
			result = val_cmp < 0 ? impl1.get () : impl2.get ();
		}
	}
	return *result;
}

btcb::wallet_value::wallet_value (btcb::mdb_val const & val_a)
{
	assert (val_a.size () == sizeof (*this));
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), key.chars.begin ());
	std::copy (reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key), reinterpret_cast<uint8_t const *> (val_a.data ()) + sizeof (key) + sizeof (work), reinterpret_cast<char *> (&work));
}

btcb::wallet_value::wallet_value (btcb::uint256_union const & key_a, uint64_t work_a) :
key (key_a),
work (work_a)
{
}

btcb::mdb_val btcb::wallet_value::val () const
{
	static_assert (sizeof (*this) == sizeof (key) + sizeof (work), "Class not packed");
	return btcb::mdb_val (sizeof (*this), const_cast<btcb::wallet_value *> (this));
}

template class btcb::mdb_iterator<btcb::pending_key, btcb::pending_info>;
template class btcb::mdb_iterator<btcb::uint256_union, btcb::block_info>;
template class btcb::mdb_iterator<btcb::uint256_union, btcb::uint128_union>;
template class btcb::mdb_iterator<btcb::uint256_union, btcb::uint256_union>;
template class btcb::mdb_iterator<btcb::uint256_union, std::shared_ptr<btcb::block>>;
template class btcb::mdb_iterator<btcb::uint256_union, std::shared_ptr<btcb::vote>>;
template class btcb::mdb_iterator<btcb::uint256_union, btcb::wallet_value>;
template class btcb::mdb_iterator<std::array<char, 64>, btcb::no_value>;

btcb::store_iterator<btcb::account, btcb::uint128_union> btcb::mdb_store::representation_begin (btcb::transaction const & transaction_a)
{
	btcb::store_iterator<btcb::account, btcb::uint128_union> result (std::make_unique<btcb::mdb_iterator<btcb::account, btcb::uint128_union>> (transaction_a, representation));
	return result;
}

btcb::store_iterator<btcb::account, btcb::uint128_union> btcb::mdb_store::representation_end ()
{
	btcb::store_iterator<btcb::account, btcb::uint128_union> result (nullptr);
	return result;
}

btcb::store_iterator<btcb::unchecked_key, btcb::unchecked_info> btcb::mdb_store::unchecked_begin (btcb::transaction const & transaction_a)
{
	btcb::store_iterator<btcb::unchecked_key, btcb::unchecked_info> result (std::make_unique<btcb::mdb_iterator<btcb::unchecked_key, btcb::unchecked_info>> (transaction_a, unchecked));
	return result;
}

btcb::store_iterator<btcb::unchecked_key, btcb::unchecked_info> btcb::mdb_store::unchecked_begin (btcb::transaction const & transaction_a, btcb::unchecked_key const & key_a)
{
	btcb::store_iterator<btcb::unchecked_key, btcb::unchecked_info> result (std::make_unique<btcb::mdb_iterator<btcb::unchecked_key, btcb::unchecked_info>> (transaction_a, unchecked, btcb::mdb_val (key_a)));
	return result;
}

btcb::store_iterator<btcb::unchecked_key, btcb::unchecked_info> btcb::mdb_store::unchecked_end ()
{
	btcb::store_iterator<btcb::unchecked_key, btcb::unchecked_info> result (nullptr);
	return result;
}

btcb::store_iterator<btcb::account, std::shared_ptr<btcb::vote>> btcb::mdb_store::vote_begin (btcb::transaction const & transaction_a)
{
	return btcb::store_iterator<btcb::account, std::shared_ptr<btcb::vote>> (std::make_unique<btcb::mdb_iterator<btcb::account, std::shared_ptr<btcb::vote>>> (transaction_a, vote));
}

btcb::store_iterator<btcb::account, std::shared_ptr<btcb::vote>> btcb::mdb_store::vote_end ()
{
	return btcb::store_iterator<btcb::account, std::shared_ptr<btcb::vote>> (nullptr);
}

btcb::mdb_store::mdb_store (bool & error_a, btcb::logger_mt & logger_a, boost::filesystem::path const & path_a, btcb::txn_tracking_config const & txn_tracking_config_a, std::chrono::milliseconds block_processor_batch_max_time_a, int lmdb_max_dbs, bool drop_unchecked, size_t const batch_size) :
logger (logger_a),
env (error_a, path_a, lmdb_max_dbs),
mdb_txn_tracker (logger_a, txn_tracking_config_a, block_processor_batch_max_time_a),
txn_tracking_enabled (txn_tracking_config_a.enable)
{
	if (!error_a)
	{
		auto is_fully_upgraded (false);
		{
			auto transaction (tx_begin_read ());
			auto err = mdb_dbi_open (env.tx (transaction), "meta", 0, &meta);
			if (err == MDB_SUCCESS)
			{
				is_fully_upgraded = (version_get (transaction) == version);
				mdb_dbi_close (env, meta);
			}
		}

		// Only open a write lock when upgrades are needed. This is because CLI commands
		// open inactive nodes which can otherwise be locked here if there is a long write
		// (can be a few minutes with the --fastbootstrap flag for instance)
		if (!is_fully_upgraded)
		{
			auto transaction (tx_begin_write ());
			open_databases (error_a, transaction, MDB_CREATE);
			if (!error_a)
			{
				error_a |= do_upgrades (transaction, batch_size);
			}
		}
		else
		{
			auto transaction (tx_begin_read ());
			open_databases (error_a, transaction, 0);
		}

		if (!error_a && drop_unchecked)
		{
			auto transaction (tx_begin_write ());
			unchecked_clear (transaction);
		}
	}
}

void btcb::mdb_store::serialize_mdb_tracker (boost::property_tree::ptree & json, std::chrono::milliseconds min_read_time, std::chrono::milliseconds min_write_time)
{
	mdb_txn_tracker.serialize_json (json, min_read_time, min_write_time);
}

btcb::write_transaction btcb::mdb_store::tx_begin_write ()
{
	return env.tx_begin_write (create_txn_callbacks ());
}

btcb::read_transaction btcb::mdb_store::tx_begin_read ()
{
	return env.tx_begin_read (create_txn_callbacks ());
}

btcb::mdb_txn_callbacks btcb::mdb_store::create_txn_callbacks ()
{
	btcb::mdb_txn_callbacks mdb_txn_callbacks;
	if (txn_tracking_enabled)
	{
		// clang-format off
		mdb_txn_callbacks.txn_start = ([&mdb_txn_tracker = mdb_txn_tracker](const btcb::transaction_impl * transaction_impl) {
			mdb_txn_tracker.add (transaction_impl);
		});
		mdb_txn_callbacks.txn_end = ([&mdb_txn_tracker = mdb_txn_tracker](const btcb::transaction_impl * transaction_impl) {
			mdb_txn_tracker.erase (transaction_impl);
		});
		// clang-format on
	}
	return mdb_txn_callbacks;
}

/**
 * This is only used with testing. If using a different store version than the latest then you may need
 * to modify some of the objects in the store to be appropriate for the version before an upgrade.
 */
void btcb::mdb_store::initialize (btcb::transaction const & transaction_a, btcb::genesis const & genesis_a)
{
	auto hash_l (genesis_a.hash ());
	assert (latest_v0_begin (transaction_a) == latest_v0_end ());
	assert (latest_v1_begin (transaction_a) == latest_v1_end ());
	btcb::block_sideband sideband (btcb::block_type::open, network_params.ledger.genesis_account, 0, network_params.ledger.genesis_amount, 1, btcb::seconds_since_epoch ());
	block_put (transaction_a, hash_l, *genesis_a.open, sideband);
	account_put (transaction_a, network_params.ledger.genesis_account, { hash_l, genesis_a.open->hash (), genesis_a.open->hash (), std::numeric_limits<btcb::uint128_t>::max (), btcb::seconds_since_epoch (), 1, 1, btcb::epoch::epoch_0 });
	representation_put (transaction_a, network_params.ledger.genesis_account, std::numeric_limits<btcb::uint128_t>::max ());
	frontier_put (transaction_a, hash_l, network_params.ledger.genesis_account);
}

void btcb::mdb_store::open_databases (bool & error_a, btcb::transaction const & transaction_a, unsigned flags)
{
	error_a |= mdb_dbi_open (env.tx (transaction_a), "frontiers", flags, &frontiers) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts", flags, &accounts_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "accounts_v1", flags, &accounts_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "send", flags, &send_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "receive", flags, &receive_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "open", flags, &open_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "change", flags, &change_blocks) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "state", flags, &state_blocks_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "state_v1", flags, &state_blocks_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pending", flags, &pending_v0) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "pending_v1", flags, &pending_v1) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "representation", flags, &representation) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "unchecked", flags, &unchecked) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "vote", flags, &vote) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "online_weight", flags, &online_weight) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "meta", flags, &meta) != 0;
	error_a |= mdb_dbi_open (env.tx (transaction_a), "peers", flags, &peers) != 0;
	if (!full_sideband (transaction_a))
	{
		error_a |= mdb_dbi_open (env.tx (transaction_a), "blocks_info", flags, &blocks_info) != 0;
	}
}

void btcb::mdb_store::version_put (btcb::transaction const & transaction_a, int version_a)
{
	btcb::uint256_union version_key (1);
	btcb::uint256_union version_value (version_a);
	auto status (mdb_put (env.tx (transaction_a), meta, btcb::mdb_val (version_key), btcb::mdb_val (version_value), 0));
	release_assert (status == 0);
	if (blocks_info == 0 && !full_sideband (transaction_a))
	{
		auto status (mdb_dbi_open (env.tx (transaction_a), "blocks_info", MDB_CREATE, &blocks_info));
		release_assert (status == MDB_SUCCESS);
	}
	if (blocks_info != 0 && full_sideband (transaction_a))
	{
		auto status (mdb_drop (env.tx (transaction_a), blocks_info, 1));
		release_assert (status == MDB_SUCCESS);
		blocks_info = 0;
	}
}

int btcb::mdb_store::version_get (btcb::transaction const & transaction_a) const
{
	btcb::uint256_union version_key (1);
	btcb::mdb_val data;
	auto error (mdb_get (env.tx (transaction_a), meta, btcb::mdb_val (version_key), data));
	int result (1);
	if (error != MDB_NOTFOUND)
	{
		btcb::uint256_union version_value (data);
		assert (version_value.qwords[2] == 0 && version_value.qwords[1] == 0 && version_value.qwords[0] == 0);
		result = version_value.number ().convert_to<int> ();
	}
	return result;
}

void btcb::mdb_store::peer_put (btcb::transaction const & transaction_a, btcb::endpoint_key const & endpoint_a)
{
	btcb::mdb_val zero (0);
	auto status (mdb_put (env.tx (transaction_a), peers, btcb::mdb_val (endpoint_a), zero, 0));
	release_assert (status == 0);
}

void btcb::mdb_store::peer_del (btcb::transaction const & transaction_a, btcb::endpoint_key const & endpoint_a)
{
	auto status (mdb_del (env.tx (transaction_a), peers, btcb::mdb_val (endpoint_a), nullptr));
	release_assert (status == 0);
}

bool btcb::mdb_store::peer_exists (btcb::transaction const & transaction_a, btcb::endpoint_key const & endpoint_a) const
{
	btcb::mdb_val junk;
	auto status (mdb_get (env.tx (transaction_a), peers, btcb::mdb_val (endpoint_a), junk));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	return (status == 0);
}

size_t btcb::mdb_store::peer_count (btcb::transaction const & transaction_a) const
{
	MDB_stat stats;
	auto status (mdb_stat (env.tx (transaction_a), peers, &stats));
	release_assert (status == 0);
	return stats.ms_entries;
}

void btcb::mdb_store::peer_clear (btcb::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), peers, 0));
	release_assert (status == 0);
}

btcb::store_iterator<btcb::endpoint_key, btcb::no_value> btcb::mdb_store::peers_begin (btcb::transaction const & transaction_a)
{
	btcb::store_iterator<btcb::endpoint_key, btcb::no_value> result (std::make_unique<btcb::mdb_iterator<btcb::endpoint_key, btcb::no_value>> (transaction_a, peers));
	return result;
}

btcb::store_iterator<btcb::endpoint_key, btcb::no_value> btcb::mdb_store::peers_end ()
{
	btcb::store_iterator<btcb::endpoint_key, btcb::no_value> result (btcb::store_iterator<btcb::endpoint_key, btcb::no_value> (nullptr));
	return result;
}

bool btcb::mdb_store::do_upgrades (btcb::write_transaction & transaction_a, size_t batch_size)
{
	auto error (false);
	auto version_l = version_get (transaction_a);
	switch (version_l)
	{
		case 1:
			upgrade_v1_to_v2 (transaction_a);
		case 2:
			upgrade_v2_to_v3 (transaction_a);
		case 3:
			upgrade_v3_to_v4 (transaction_a);
		case 4:
			upgrade_v4_to_v5 (transaction_a);
		case 5:
			upgrade_v5_to_v6 (transaction_a);
		case 6:
			upgrade_v6_to_v7 (transaction_a);
		case 7:
			upgrade_v7_to_v8 (transaction_a);
		case 8:
			upgrade_v8_to_v9 (transaction_a);
		case 9:
			upgrade_v9_to_v10 (transaction_a);
		case 10:
			upgrade_v10_to_v11 (transaction_a);
		case 11:
			upgrade_v11_to_v12 (transaction_a);
		case 12:
			upgrade_v12_to_v13 (transaction_a, batch_size);
		case 13:
			upgrade_v13_to_v14 (transaction_a);
		case 14:
			break;
		default:
			logger.always_log (boost::str (boost::format ("The version of the ledger (%1%) is too high for this node") % version_l));
			error = true;
			break;
	}
	return error;
}

void btcb::mdb_store::upgrade_v1_to_v2 (btcb::transaction const & transaction_a)
{
	version_put (transaction_a, 2);
	btcb::account account (1);
	while (!account.is_zero ())
	{
		btcb::mdb_iterator<btcb::uint256_union, btcb::account_info_v1> i (transaction_a, accounts_v0, btcb::mdb_val (account));
		std::cerr << std::hex;
		if (i != btcb::mdb_iterator<btcb::uint256_union, btcb::account_info_v1> (nullptr))
		{
			account = btcb::uint256_union (i->first);
			btcb::account_info_v1 v1 (i->second);
			btcb::account_info_v5 v2;
			v2.balance = v1.balance;
			v2.head = v1.head;
			v2.modified = v1.modified;
			v2.rep_block = v1.rep_block;
			auto block (block_get (transaction_a, v1.head));
			while (!block->previous ().is_zero ())
			{
				block = block_get (transaction_a, block->previous ());
			}
			v2.open_block = block->hash ();
			auto status (mdb_put (env.tx (transaction_a), accounts_v0, btcb::mdb_val (account), v2.val (), 0));
			release_assert (status == 0);
			account = account.number () + 1;
		}
		else
		{
			account.clear ();
		}
	}
}

void btcb::mdb_store::upgrade_v2_to_v3 (btcb::transaction const & transaction_a)
{
	version_put (transaction_a, 3);
	mdb_drop (env.tx (transaction_a), representation, 0);
	for (auto i (std::make_unique<btcb::mdb_iterator<btcb::account, btcb::account_info_v5>> (transaction_a, accounts_v0)), n (std::make_unique<btcb::mdb_iterator<btcb::account, btcb::account_info_v5>> (nullptr)); *i != *n; ++(*i))
	{
		btcb::account account_l ((*i)->first);
		btcb::account_info_v5 info ((*i)->second);
		representative_visitor visitor (transaction_a, *this);
		visitor.compute (info.head);
		assert (!visitor.result.is_zero ());
		info.rep_block = visitor.result;
		auto impl (boost::polymorphic_downcast<btcb::mdb_iterator<btcb::account, btcb::account_info_v5> *> (i.get ()));
		mdb_cursor_put (impl->cursor, btcb::mdb_val (account_l), info.val (), MDB_CURRENT);
		representation_add (transaction_a, visitor.result, info.balance.number ());
	}
}

void btcb::mdb_store::upgrade_v3_to_v4 (btcb::transaction const & transaction_a)
{
	version_put (transaction_a, 4);
	std::queue<std::pair<btcb::pending_key, btcb::pending_info>> items;
	for (auto i (btcb::store_iterator<btcb::block_hash, btcb::pending_info_v3> (std::make_unique<btcb::mdb_iterator<btcb::block_hash, btcb::pending_info_v3>> (transaction_a, pending_v0))), n (btcb::store_iterator<btcb::block_hash, btcb::pending_info_v3> (nullptr)); i != n; ++i)
	{
		btcb::block_hash hash (i->first);
		btcb::pending_info_v3 info (i->second);
		items.push (std::make_pair (btcb::pending_key (info.destination, hash), btcb::pending_info (info.source, info.amount, btcb::epoch::epoch_0)));
	}
	mdb_drop (env.tx (transaction_a), pending_v0, 0);
	while (!items.empty ())
	{
		pending_put (transaction_a, items.front ().first, items.front ().second);
		items.pop ();
	}
}

void btcb::mdb_store::upgrade_v4_to_v5 (btcb::transaction const & transaction_a)
{
	version_put (transaction_a, 5);
	for (auto i (btcb::store_iterator<btcb::account, btcb::account_info_v5> (std::make_unique<btcb::mdb_iterator<btcb::account, btcb::account_info_v5>> (transaction_a, accounts_v0))), n (btcb::store_iterator<btcb::account, btcb::account_info_v5> (nullptr)); i != n; ++i)
	{
		btcb::account_info_v5 info (i->second);
		btcb::block_hash successor (0);
		auto block (block_get (transaction_a, info.head));
		while (block != nullptr)
		{
			auto hash (block->hash ());
			if (block_successor (transaction_a, hash).is_zero () && !successor.is_zero ())
			{
				std::vector<uint8_t> vector;
				{
					btcb::vectorstream stream (vector);
					block->serialize (stream);
					btcb::write (stream, successor.bytes);
				}
				block_raw_put (transaction_a, block_database (block->type (), btcb::epoch::epoch_0), hash, { vector.size (), vector.data () });
				if (!block->previous ().is_zero ())
				{
					btcb::block_type type;
					auto value (block_raw_get (transaction_a, block->previous (), type));
					auto version (block_version (transaction_a, block->previous ()));
					assert (value.mv_size != 0);
					std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
					std::copy (hash.bytes.begin (), hash.bytes.end (), data.end () - btcb::block_sideband::size (type));
					block_raw_put (transaction_a, block_database (type, version), block->previous (), btcb::mdb_val (data.size (), data.data ()));
				}
			}
			successor = hash;
			block = block_get (transaction_a, block->previous ());
		}
	}
}

void btcb::mdb_store::upgrade_v5_to_v6 (btcb::transaction const & transaction_a)
{
	version_put (transaction_a, 6);
	std::deque<std::pair<btcb::account, btcb::account_info_v13>> headers;
	for (auto i (btcb::store_iterator<btcb::account, btcb::account_info_v5> (std::make_unique<btcb::mdb_iterator<btcb::account, btcb::account_info_v5>> (transaction_a, accounts_v0))), n (btcb::store_iterator<btcb::account, btcb::account_info_v5> (nullptr)); i != n; ++i)
	{
		btcb::account account (i->first);
		btcb::account_info_v5 info_old (i->second);
		uint64_t block_count (0);
		auto hash (info_old.head);
		while (!hash.is_zero ())
		{
			++block_count;
			auto block (block_get (transaction_a, hash));
			assert (block != nullptr);
			hash = block->previous ();
		}
		headers.emplace_back (account, btcb::account_info_v13{ info_old.head, info_old.rep_block, info_old.open_block, info_old.balance, info_old.modified, block_count, btcb::epoch::epoch_0 });
	}
	for (auto i (headers.begin ()), n (headers.end ()); i != n; ++i)
	{
		auto status (mdb_put (env.tx (transaction_a), accounts_v0, btcb::mdb_val (i->first), btcb::mdb_val (i->second), 0));
		release_assert (status == 0);
	}
}

void btcb::mdb_store::upgrade_v6_to_v7 (btcb::transaction const & transaction_a)
{
	version_put (transaction_a, 7);
	mdb_drop (env.tx (transaction_a), unchecked, 0);
}

void btcb::mdb_store::upgrade_v7_to_v8 (btcb::transaction const & transaction_a)
{
	version_put (transaction_a, 8);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE | MDB_DUPSORT, &unchecked);
}

void btcb::mdb_store::upgrade_v8_to_v9 (btcb::transaction const & transaction_a)
{
	version_put (transaction_a, 9);
	MDB_dbi sequence;
	mdb_dbi_open (env.tx (transaction_a), "sequence", MDB_CREATE | MDB_DUPSORT, &sequence);
	btcb::genesis genesis;
	std::shared_ptr<btcb::block> block (std::move (genesis.open));
	btcb::keypair junk;
	for (btcb::mdb_iterator<btcb::account, uint64_t> i (transaction_a, sequence), n (btcb::mdb_iterator<btcb::account, uint64_t> (nullptr)); i != n; ++i)
	{
		btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (i->second.data ()), i->second.size ());
		uint64_t sequence;
		auto error (btcb::try_read (stream, sequence));
		// Create a dummy vote with the same sequence number for easy upgrading.  This won't have a valid signature.
		btcb::vote dummy (btcb::account (i->first), junk.prv, sequence, block);
		std::vector<uint8_t> vector;
		{
			btcb::vectorstream stream (vector);
			dummy.serialize (stream);
		}
		auto status1 (mdb_put (env.tx (transaction_a), vote, btcb::mdb_val (i->first), btcb::mdb_val (vector.size (), vector.data ()), 0));
		release_assert (status1 == 0);
		assert (!error);
	}
	mdb_drop (env.tx (transaction_a), sequence, 1);
}

void btcb::mdb_store::upgrade_v9_to_v10 (btcb::transaction const & transaction_a)
{
}

void btcb::mdb_store::upgrade_v10_to_v11 (btcb::transaction const & transaction_a)
{
	version_put (transaction_a, 11);
	MDB_dbi unsynced;
	mdb_dbi_open (env.tx (transaction_a), "unsynced", MDB_CREATE | MDB_DUPSORT, &unsynced);
	mdb_drop (env.tx (transaction_a), unsynced, 1);
}

void btcb::mdb_store::upgrade_v11_to_v12 (btcb::transaction const & transaction_a)
{
	version_put (transaction_a, 12);
	mdb_drop (env.tx (transaction_a), unchecked, 1);
	mdb_dbi_open (env.tx (transaction_a), "unchecked", MDB_CREATE, &unchecked);
	MDB_dbi checksum;
	mdb_dbi_open (env.tx (transaction_a), "checksum", MDB_CREATE, &checksum);
	mdb_drop (env.tx (transaction_a), checksum, 1);
}

void btcb::mdb_store::upgrade_v12_to_v13 (btcb::write_transaction & transaction_a, size_t const batch_size)
{
	size_t cost (0);
	btcb::account account (0);
	auto const & not_an_account (network_params.random.not_an_account);
	while (account != not_an_account)
	{
		btcb::account first (0);
		btcb::account_info_v13 second;
		{
			btcb::store_iterator<btcb::account, btcb::account_info_v13> current (std::make_unique<btcb::mdb_merge_iterator<btcb::account, btcb::account_info_v13>> (transaction_a, accounts_v0, accounts_v1, btcb::mdb_val (account)));
			btcb::store_iterator<btcb::account, btcb::account_info_v13> end (nullptr);
			if (current != end)
			{
				first = current->first;
				second = current->second;
			}
		}
		if (!first.is_zero ())
		{
			auto hash (second.open_block);
			uint64_t height (1);
			btcb::block_sideband sideband;
			while (!hash.is_zero ())
			{
				if (cost >= batch_size)
				{
					logger.always_log (boost::str (boost::format ("Upgrading sideband information for account %1%... height %2%") % first.to_account ().substr (0, 24) % std::to_string (height)));
					transaction_a.commit ();
					std::this_thread::yield ();
					transaction_a.renew ();
					cost = 0;
				}
				auto block (block_get (transaction_a, hash, &sideband));
				assert (block != nullptr);
				if (sideband.height == 0)
				{
					sideband.height = height;
					block_put (transaction_a, hash, *block, sideband, block_version (transaction_a, hash));
					cost += 16;
				}
				else
				{
					cost += 1;
				}
				hash = sideband.successor;
				++height;
			}
			account = first.number () + 1;
		}
		else
		{
			account = not_an_account;
		}
	}
	if (account == not_an_account)
	{
		logger.always_log (boost::str (boost::format ("Completed sideband upgrade")));
		version_put (transaction_a, 13);
	}
}

void btcb::mdb_store::upgrade_v13_to_v14 (btcb::transaction const & transaction_a)
{
	// Upgrade all accounts to have a confirmation of 0
	version_put (transaction_a, 14);
	btcb::store_iterator<btcb::account, btcb::account_info_v13> i (std::make_unique<btcb::mdb_merge_iterator<btcb::account, btcb::account_info_v13>> (transaction_a, accounts_v0, accounts_v1));
	btcb::store_iterator<btcb::account, btcb::account_info_v13> n (nullptr);
	constexpr uint64_t zeroed_confirmation_height (0);

	std::vector<std::pair<btcb::account, btcb::account_info>> account_infos;
	account_infos.reserve (account_count (transaction_a));
	for (; i != n; ++i)
	{
		btcb::account_info_v13 account_info_v13 (i->second);
		account_infos.emplace_back (i->first, btcb::account_info{ account_info_v13.head, account_info_v13.rep_block, account_info_v13.open_block, account_info_v13.balance, account_info_v13.modified, account_info_v13.block_count, zeroed_confirmation_height, account_info_v13.epoch });
	}

	for (auto const & account_info : account_infos)
	{
		account_put (transaction_a, account_info.first, account_info.second);
	}

	btcb::uint256_union node_id_mdb_key (3);
	auto error (mdb_del (env.tx (transaction_a), meta, btcb::mdb_val (node_id_mdb_key), nullptr));
	release_assert (!error || error == MDB_NOTFOUND);
}

void btcb::mdb_store::clear (MDB_dbi db_a)
{
	auto transaction (tx_begin_write ());
	auto status (mdb_drop (env.tx (transaction), db_a, 0));
	release_assert (status == 0);
}

btcb::uint128_t btcb::mdb_store::block_balance (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a)
{
	btcb::block_sideband sideband;
	auto block (block_get (transaction_a, hash_a, &sideband));
	btcb::uint128_t result;
	switch (block->type ())
	{
		case btcb::block_type::open:
		case btcb::block_type::receive:
		case btcb::block_type::change:
			result = sideband.balance.number ();
			break;
		case btcb::block_type::send:
			result = boost::polymorphic_downcast<btcb::send_block *> (block.get ())->hashables.balance.number ();
			break;
		case btcb::block_type::state:
			result = boost::polymorphic_downcast<btcb::state_block *> (block.get ())->hashables.balance.number ();
			break;
		case btcb::block_type::invalid:
		case btcb::block_type::not_a_block:
			release_assert (false);
			break;
	}
	return result;
}

btcb::uint128_t btcb::mdb_store::block_balance_computed (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a) const
{
	assert (!full_sideband (transaction_a));
	summation_visitor visitor (transaction_a, *this);
	return visitor.compute_balance (hash_a);
}

btcb::epoch btcb::mdb_store::block_version (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a)
{
	btcb::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), state_blocks_v1, btcb::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	return status == 0 ? btcb::epoch::epoch_1 : btcb::epoch::epoch_0;
}

void btcb::mdb_store::representation_add (btcb::transaction const & transaction_a, btcb::block_hash const & source_a, btcb::uint128_t const & amount_a)
{
	auto source_block (block_get (transaction_a, source_a));
	assert (source_block != nullptr);
	auto source_rep (source_block->representative ());
	auto source_previous (representation_get (transaction_a, source_rep));
	representation_put (transaction_a, source_rep, source_previous + amount_a);
}

MDB_dbi btcb::mdb_store::block_database (btcb::block_type type_a, btcb::epoch epoch_a)
{
	if (type_a == btcb::block_type::state)
	{
		assert (epoch_a == btcb::epoch::epoch_0 || epoch_a == btcb::epoch::epoch_1);
	}
	else
	{
		assert (epoch_a == btcb::epoch::epoch_0);
	}
	MDB_dbi result;
	switch (type_a)
	{
		case btcb::block_type::send:
			result = send_blocks;
			break;
		case btcb::block_type::receive:
			result = receive_blocks;
			break;
		case btcb::block_type::open:
			result = open_blocks;
			break;
		case btcb::block_type::change:
			result = change_blocks;
			break;
		case btcb::block_type::state:
			switch (epoch_a)
			{
				case btcb::epoch::epoch_0:
					result = state_blocks_v0;
					break;
				case btcb::epoch::epoch_1:
					result = state_blocks_v1;
					break;
				default:
					assert (false);
			}
			break;
		default:
			assert (false);
			break;
	}
	return result;
}

void btcb::mdb_store::block_raw_put (btcb::transaction const & transaction_a, MDB_dbi database_a, btcb::block_hash const & hash_a, MDB_val value_a)
{
	auto status2 (mdb_put (env.tx (transaction_a), database_a, btcb::mdb_val (hash_a), &value_a, 0));
	release_assert (status2 == 0);
}

void btcb::mdb_store::block_put (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a, btcb::block const & block_a, btcb::block_sideband const & sideband_a, btcb::epoch epoch_a)
{
	assert (block_a.type () == sideband_a.type);
	assert (sideband_a.successor.is_zero () || block_exists (transaction_a, sideband_a.successor));
	std::vector<uint8_t> vector;
	{
		btcb::vectorstream stream (vector);
		block_a.serialize (stream);
		sideband_a.serialize (stream);
	}
	block_raw_put (transaction_a, block_database (block_a.type (), epoch_a), hash_a, { vector.size (), vector.data () });
	btcb::block_predecessor_set predecessor (transaction_a, *this);
	block_a.visit (predecessor);
	assert (block_a.previous ().is_zero () || block_successor (transaction_a, block_a.previous ()) == hash_a);
}

boost::optional<MDB_val> btcb::mdb_store::block_raw_get_by_type (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a, btcb::block_type & type_a) const
{
	btcb::mdb_val value;
	auto status (MDB_NOTFOUND);
	switch (type_a)
	{
		case btcb::block_type::send:
		{
			status = mdb_get (env.tx (transaction_a), send_blocks, btcb::mdb_val (hash_a), value);
			break;
		}
		case btcb::block_type::receive:
		{
			status = mdb_get (env.tx (transaction_a), receive_blocks, btcb::mdb_val (hash_a), value);
			break;
		}
		case btcb::block_type::open:
		{
			status = mdb_get (env.tx (transaction_a), open_blocks, btcb::mdb_val (hash_a), value);
			break;
		}
		case btcb::block_type::change:
		{
			status = mdb_get (env.tx (transaction_a), change_blocks, btcb::mdb_val (hash_a), value);
			break;
		}
		case btcb::block_type::state:
		{
			status = mdb_get (env.tx (transaction_a), state_blocks_v1, btcb::mdb_val (hash_a), value);
			if (status != 0)
			{
				status = mdb_get (env.tx (transaction_a), state_blocks_v0, btcb::mdb_val (hash_a), value);
			}
			break;
		}
		case btcb::block_type::invalid:
		case btcb::block_type::not_a_block:
		{
			break;
		}
	}

	release_assert (status == MDB_SUCCESS || status == MDB_NOTFOUND);
	boost::optional<MDB_val> result;
	if (status == MDB_SUCCESS)
	{
		result = value;
	}

	return result;
}

MDB_val btcb::mdb_store::block_raw_get (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a, btcb::block_type & type_a) const
{
	btcb::mdb_val result;
	// Table lookups are ordered by match probability
	btcb::block_type block_types[]{ btcb::block_type::state, btcb::block_type::send, btcb::block_type::receive, btcb::block_type::open, btcb::block_type::change };
	for (auto current_type : block_types)
	{
		auto mdb_val (block_raw_get_by_type (transaction_a, hash_a, current_type));
		if (mdb_val.is_initialized ())
		{
			type_a = current_type;
			result = mdb_val.get ();
			break;
		}
	}

	return result;
}

template <typename T>
std::shared_ptr<btcb::block> btcb::mdb_store::block_random (btcb::transaction const & transaction_a, MDB_dbi database)
{
	btcb::block_hash hash;
	btcb::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
	btcb::store_iterator<btcb::block_hash, std::shared_ptr<T>> existing (std::make_unique<btcb::mdb_iterator<btcb::block_hash, std::shared_ptr<T>>> (transaction_a, database, btcb::mdb_val (hash)));
	if (existing == btcb::store_iterator<btcb::block_hash, std::shared_ptr<T>> (nullptr))
	{
		existing = btcb::store_iterator<btcb::block_hash, std::shared_ptr<T>> (std::make_unique<btcb::mdb_iterator<btcb::block_hash, std::shared_ptr<T>>> (transaction_a, database));
	}
	auto end (btcb::store_iterator<btcb::block_hash, std::shared_ptr<T>> (nullptr));
	assert (existing != end);
	return block_get (transaction_a, btcb::block_hash (existing->first));
}

std::shared_ptr<btcb::block> btcb::mdb_store::block_random (btcb::transaction const & transaction_a)
{
	auto count (block_count (transaction_a));
	release_assert (std::numeric_limits<CryptoPP::word32>::max () > count.sum ());
	auto region = static_cast<size_t> (btcb::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (count.sum () - 1)));
	std::shared_ptr<btcb::block> result;
	if (region < count.send)
	{
		result = block_random<btcb::send_block> (transaction_a, send_blocks);
	}
	else
	{
		region -= count.send;
		if (region < count.receive)
		{
			result = block_random<btcb::receive_block> (transaction_a, receive_blocks);
		}
		else
		{
			region -= count.receive;
			if (region < count.open)
			{
				result = block_random<btcb::open_block> (transaction_a, open_blocks);
			}
			else
			{
				region -= count.open;
				if (region < count.change)
				{
					result = block_random<btcb::change_block> (transaction_a, change_blocks);
				}
				else
				{
					region -= count.change;
					if (region < count.state_v0)
					{
						result = block_random<btcb::state_block> (transaction_a, state_blocks_v0);
					}
					else
					{
						result = block_random<btcb::state_block> (transaction_a, state_blocks_v1);
					}
				}
			}
		}
	}
	assert (result != nullptr);
	return result;
}

bool btcb::mdb_store::full_sideband (btcb::transaction const & transaction_a) const
{
	return version_get (transaction_a) > 12;
}

bool btcb::mdb_store::entry_has_sideband (MDB_val entry_a, btcb::block_type type_a) const
{
	return entry_a.mv_size == btcb::block::size (type_a) + btcb::block_sideband::size (type_a);
}

size_t btcb::mdb_store::block_successor_offset (btcb::transaction const & transaction_a, MDB_val entry_a, btcb::block_type type_a) const
{
	size_t result;
	if (full_sideband (transaction_a) || entry_has_sideband (entry_a, type_a))
	{
		result = entry_a.mv_size - btcb::block_sideband::size (type_a);
	}
	else
	{
		// Read old successor-only sideband
		assert (entry_a.mv_size == btcb::block::size (type_a) + sizeof (btcb::uint256_union));
		result = entry_a.mv_size - sizeof (btcb::uint256_union);
	}
	return result;
}

btcb::block_hash btcb::mdb_store::block_successor (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a) const
{
	btcb::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	btcb::block_hash result;
	if (value.mv_size != 0)
	{
		assert (value.mv_size >= result.bytes.size ());
		btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data) + block_successor_offset (transaction_a, value, type), result.bytes.size ());
		auto error (btcb::try_read (stream, result.bytes));
		assert (!error);
	}
	else
	{
		result.clear ();
	}
	return result;
}

void btcb::mdb_store::block_successor_clear (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a)
{
	btcb::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	auto version (block_version (transaction_a, hash_a));
	assert (value.mv_size != 0);
	std::vector<uint8_t> data (static_cast<uint8_t *> (value.mv_data), static_cast<uint8_t *> (value.mv_data) + value.mv_size);
	std::fill_n (data.begin () + block_successor_offset (transaction_a, value, type), sizeof (btcb::uint256_union), 0);
	block_raw_put (transaction_a, block_database (type, version), hash_a, btcb::mdb_val (data.size (), data.data ()));
}

// Converts a block hash to a block height
uint64_t btcb::mdb_store::block_account_height (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a) const
{
	btcb::block_sideband sideband;
	auto block = block_get (transaction_a, hash_a, &sideband);
	assert (block != nullptr);
	return sideband.height;
}

std::shared_ptr<btcb::block> btcb::mdb_store::block_get (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a, btcb::block_sideband * sideband_a) const
{
	btcb::block_type type;
	auto value (block_raw_get (transaction_a, hash_a, type));
	std::shared_ptr<btcb::block> result;
	if (value.mv_size != 0)
	{
		btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.mv_data), value.mv_size);
		result = btcb::deserialize_block (stream, type);
		assert (result != nullptr);
		if (sideband_a)
		{
			sideband_a->type = type;
			if (full_sideband (transaction_a) || entry_has_sideband (value, type))
			{
				auto error (sideband_a->deserialize (stream));
				assert (!error);
			}
			else
			{
				// Reconstruct sideband data for block.
				sideband_a->account = block_account_computed (transaction_a, hash_a);
				sideband_a->balance = block_balance_computed (transaction_a, hash_a);
				sideband_a->successor = block_successor (transaction_a, hash_a);
				sideband_a->height = 0;
				sideband_a->timestamp = 0;
			}
		}
	}
	return result;
}

void btcb::mdb_store::block_del (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a)
{
	auto status (mdb_del (env.tx (transaction_a), state_blocks_v1, btcb::mdb_val (hash_a), nullptr));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status != 0)
	{
		auto status (mdb_del (env.tx (transaction_a), state_blocks_v0, btcb::mdb_val (hash_a), nullptr));
		release_assert (status == 0 || status == MDB_NOTFOUND);
		if (status != 0)
		{
			auto status (mdb_del (env.tx (transaction_a), send_blocks, btcb::mdb_val (hash_a), nullptr));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			if (status != 0)
			{
				auto status (mdb_del (env.tx (transaction_a), receive_blocks, btcb::mdb_val (hash_a), nullptr));
				release_assert (status == 0 || status == MDB_NOTFOUND);
				if (status != 0)
				{
					auto status (mdb_del (env.tx (transaction_a), open_blocks, btcb::mdb_val (hash_a), nullptr));
					release_assert (status == 0 || status == MDB_NOTFOUND);
					if (status != 0)
					{
						auto status (mdb_del (env.tx (transaction_a), change_blocks, btcb::mdb_val (hash_a), nullptr));
						release_assert (status == 0);
					}
				}
			}
		}
	}
}

bool btcb::mdb_store::block_exists (btcb::transaction const & transaction_a, btcb::block_type type, btcb::block_hash const & hash_a)
{
	auto exists (false);
	btcb::mdb_val junk;

	switch (type)
	{
		case btcb::block_type::send:
		{
			auto status (mdb_get (env.tx (transaction_a), send_blocks, btcb::mdb_val (hash_a), junk));
			assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case btcb::block_type::receive:
		{
			auto status (mdb_get (env.tx (transaction_a), receive_blocks, btcb::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case btcb::block_type::open:
		{
			auto status (mdb_get (env.tx (transaction_a), open_blocks, btcb::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case btcb::block_type::change:
		{
			auto status (mdb_get (env.tx (transaction_a), change_blocks, btcb::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			break;
		}
		case btcb::block_type::state:
		{
			auto status (mdb_get (env.tx (transaction_a), state_blocks_v0, btcb::mdb_val (hash_a), junk));
			release_assert (status == 0 || status == MDB_NOTFOUND);
			exists = status == 0;
			if (!exists)
			{
				auto status (mdb_get (env.tx (transaction_a), state_blocks_v1, btcb::mdb_val (hash_a), junk));
				release_assert (status == 0 || status == MDB_NOTFOUND);
				exists = status == 0;
			}
			break;
		}
		case btcb::block_type::invalid:
		case btcb::block_type::not_a_block:
			break;
	}

	return exists;
}

bool btcb::mdb_store::block_exists (btcb::transaction const & tx_a, btcb::block_hash const & hash_a)
{
	// Table lookups are ordered by match probability
	// clang-format off
	return
		block_exists (tx_a, btcb::block_type::state, hash_a) ||
		block_exists (tx_a, btcb::block_type::send, hash_a) ||
		block_exists (tx_a, btcb::block_type::receive, hash_a) ||
		block_exists (tx_a, btcb::block_type::open, hash_a) ||
		block_exists (tx_a, btcb::block_type::change, hash_a);
	// clang-format on
}

btcb::block_counts btcb::mdb_store::block_count (btcb::transaction const & transaction_a)
{
	btcb::block_counts result;
	MDB_stat send_stats;
	auto status1 (mdb_stat (env.tx (transaction_a), send_blocks, &send_stats));
	release_assert (status1 == 0);
	MDB_stat receive_stats;
	auto status2 (mdb_stat (env.tx (transaction_a), receive_blocks, &receive_stats));
	release_assert (status2 == 0);
	MDB_stat open_stats;
	auto status3 (mdb_stat (env.tx (transaction_a), open_blocks, &open_stats));
	release_assert (status3 == 0);
	MDB_stat change_stats;
	auto status4 (mdb_stat (env.tx (transaction_a), change_blocks, &change_stats));
	release_assert (status4 == 0);
	MDB_stat state_v0_stats;
	auto status5 (mdb_stat (env.tx (transaction_a), state_blocks_v0, &state_v0_stats));
	release_assert (status5 == 0);
	MDB_stat state_v1_stats;
	auto status6 (mdb_stat (env.tx (transaction_a), state_blocks_v1, &state_v1_stats));
	release_assert (status6 == 0);
	result.send = send_stats.ms_entries;
	result.receive = receive_stats.ms_entries;
	result.open = open_stats.ms_entries;
	result.change = change_stats.ms_entries;
	result.state_v0 = state_v0_stats.ms_entries;
	result.state_v1 = state_v1_stats.ms_entries;
	return result;
}

bool btcb::mdb_store::root_exists (btcb::transaction const & transaction_a, btcb::uint256_union const & root_a)
{
	return block_exists (transaction_a, root_a) || account_exists (transaction_a, root_a);
}

bool btcb::mdb_store::source_exists (btcb::transaction const & transaction_a, btcb::block_hash const & source_a)
{
	return block_exists (transaction_a, btcb::block_type::state, source_a) || block_exists (transaction_a, btcb::block_type::send, source_a);
}

btcb::account btcb::mdb_store::block_account (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a) const
{
	btcb::block_sideband sideband;
	auto block (block_get (transaction_a, hash_a, &sideband));
	btcb::account result (block->account ());
	if (result.is_zero ())
	{
		result = sideband.account;
	}
	assert (!result.is_zero ());
	return result;
}

// Return account containing hash
btcb::account btcb::mdb_store::block_account_computed (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a) const
{
	assert (!full_sideband (transaction_a));
	btcb::account result (0);
	auto hash (hash_a);
	while (result.is_zero ())
	{
		auto block (block_get (transaction_a, hash));
		assert (block);
		result = block->account ();
		if (result.is_zero ())
		{
			auto type (btcb::block_type::invalid);
			auto value (block_raw_get (transaction_a, block->previous (), type));
			if (entry_has_sideband (value, type))
			{
				result = block_account (transaction_a, block->previous ());
			}
			else
			{
				btcb::block_info block_info;
				if (!block_info_get (transaction_a, hash, block_info))
				{
					result = block_info.account;
				}
				else
				{
					result = frontier_get (transaction_a, hash);
					if (result.is_zero ())
					{
						auto successor (block_successor (transaction_a, hash));
						assert (!successor.is_zero ());
						hash = successor;
					}
				}
			}
		}
	}
	assert (!result.is_zero ());
	return result;
}

void btcb::mdb_store::account_del (btcb::transaction const & transaction_a, btcb::account const & account_a)
{
	auto status1 (mdb_del (env.tx (transaction_a), accounts_v1, btcb::mdb_val (account_a), nullptr));
	if (status1 != 0)
	{
		release_assert (status1 == MDB_NOTFOUND);
		auto status2 (mdb_del (env.tx (transaction_a), accounts_v0, btcb::mdb_val (account_a), nullptr));
		release_assert (status2 == 0);
	}
}

bool btcb::mdb_store::account_exists (btcb::transaction const & transaction_a, btcb::account const & account_a)
{
	auto iterator (latest_begin (transaction_a, account_a));
	return iterator != latest_end () && btcb::account (iterator->first) == account_a;
}

bool btcb::mdb_store::account_get (btcb::transaction const & transaction_a, btcb::account const & account_a, btcb::account_info & info_a)
{
	btcb::mdb_val value;
	auto status1 (mdb_get (env.tx (transaction_a), accounts_v1, btcb::mdb_val (account_a), value));
	release_assert (status1 == 0 || status1 == MDB_NOTFOUND);
	bool result (false);
	btcb::epoch epoch;
	if (status1 == 0)
	{
		epoch = btcb::epoch::epoch_1;
	}
	else
	{
		auto status2 (mdb_get (env.tx (transaction_a), accounts_v0, btcb::mdb_val (account_a), value));
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 == 0)
		{
			epoch = btcb::epoch::epoch_0;
		}
		else
		{
			result = true;
		}
	}
	if (!result)
	{
		btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		info_a.epoch = epoch;
		result = info_a.deserialize (stream);
	}
	return result;
}

void btcb::mdb_store::frontier_put (btcb::transaction const & transaction_a, btcb::block_hash const & block_a, btcb::account const & account_a)
{
	auto status (mdb_put (env.tx (transaction_a), frontiers, btcb::mdb_val (block_a), btcb::mdb_val (account_a), 0));
	release_assert (status == 0);
}

btcb::account btcb::mdb_store::frontier_get (btcb::transaction const & transaction_a, btcb::block_hash const & block_a) const
{
	btcb::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), frontiers, btcb::mdb_val (block_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	btcb::account result (0);
	if (status == 0)
	{
		result = btcb::uint256_union (value);
	}
	return result;
}

void btcb::mdb_store::frontier_del (btcb::transaction const & transaction_a, btcb::block_hash const & block_a)
{
	auto status (mdb_del (env.tx (transaction_a), frontiers, btcb::mdb_val (block_a), nullptr));
	release_assert (status == 0);
}

size_t btcb::mdb_store::account_count (btcb::transaction const & transaction_a)
{
	MDB_stat stats1;
	auto status1 (mdb_stat (env.tx (transaction_a), accounts_v0, &stats1));
	release_assert (status1 == 0);
	MDB_stat stats2;
	auto status2 (mdb_stat (env.tx (transaction_a), accounts_v1, &stats2));
	release_assert (status2 == 0);
	auto result (stats1.ms_entries + stats2.ms_entries);
	return result;
}

MDB_dbi btcb::mdb_store::get_account_db (btcb::epoch epoch_a) const
{
	MDB_dbi db;
	switch (epoch_a)
	{
		case btcb::epoch::invalid:
		case btcb::epoch::unspecified:
			assert (false);
		case btcb::epoch::epoch_0:
			db = accounts_v0;
			break;
		case btcb::epoch::epoch_1:
			db = accounts_v1;
			break;
	}
	return db;
}

MDB_dbi btcb::mdb_store::get_pending_db (btcb::epoch epoch_a) const
{
	MDB_dbi db;
	switch (epoch_a)
	{
		case btcb::epoch::invalid:
		case btcb::epoch::unspecified:
			assert (false);
		case btcb::epoch::epoch_0:
			db = pending_v0;
			break;
		case btcb::epoch::epoch_1:
			db = pending_v1;
			break;
	}
	return db;
}

void btcb::mdb_store::account_put (btcb::transaction const & transaction_a, btcb::account const & account_a, btcb::account_info const & info_a)
{
	auto status (mdb_put (env.tx (transaction_a), get_account_db (info_a.epoch), btcb::mdb_val (account_a), btcb::mdb_val (info_a), 0));
	release_assert (status == 0);
}

void btcb::mdb_store::confirmation_height_clear (btcb::transaction const & transaction_a, btcb::account const & account, btcb::account_info const & account_info)
{
	btcb::account_info info_copy (account_info);
	if (info_copy.confirmation_height > 0)
	{
		info_copy.confirmation_height = 0;
		account_put (transaction_a, account, info_copy);
	}
}

void btcb::mdb_store::confirmation_height_clear (btcb::transaction const & transaction_a)
{
	for (auto i (latest_begin (transaction_a)), n (latest_end ()); i != n; ++i)
	{
		confirmation_height_clear (transaction_a, i->first, i->second);
	}
}

void btcb::mdb_store::pending_put (btcb::transaction const & transaction_a, btcb::pending_key const & key_a, btcb::pending_info const & pending_a)
{
	auto status (mdb_put (env.tx (transaction_a), get_pending_db (pending_a.epoch), btcb::mdb_val (key_a), btcb::mdb_val (pending_a), 0));
	release_assert (status == 0);
}

void btcb::mdb_store::pending_del (btcb::transaction const & transaction_a, btcb::pending_key const & key_a)
{
	auto status1 (mdb_del (env.tx (transaction_a), pending_v1, mdb_val (key_a), nullptr));
	if (status1 != 0)
	{
		release_assert (status1 == MDB_NOTFOUND);
		auto status2 (mdb_del (env.tx (transaction_a), pending_v0, mdb_val (key_a), nullptr));
		release_assert (status2 == 0);
	}
}

bool btcb::mdb_store::pending_exists (btcb::transaction const & transaction_a, btcb::pending_key const & key_a)
{
	auto iterator (pending_begin (transaction_a, key_a));
	return iterator != pending_end () && btcb::pending_key (iterator->first) == key_a;
}

bool btcb::mdb_store::pending_get (btcb::transaction const & transaction_a, btcb::pending_key const & key_a, btcb::pending_info & pending_a)
{
	btcb::mdb_val value;
	auto status1 (mdb_get (env.tx (transaction_a), pending_v1, mdb_val (key_a), value));
	release_assert (status1 == 0 || status1 == MDB_NOTFOUND);
	bool result (false);
	btcb::epoch epoch;
	if (status1 == 0)
	{
		epoch = btcb::epoch::epoch_1;
	}
	else
	{
		auto status2 (mdb_get (env.tx (transaction_a), pending_v0, mdb_val (key_a), value));
		release_assert (status2 == 0 || status2 == MDB_NOTFOUND);
		if (status2 == 0)
		{
			epoch = btcb::epoch::epoch_0;
		}
		else
		{
			result = true;
		}
	}
	if (!result)
	{
		btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		pending_a.epoch = epoch;
		result = pending_a.deserialize (stream);
	}
	return result;
}

btcb::store_iterator<btcb::pending_key, btcb::pending_info> btcb::mdb_store::pending_begin (btcb::transaction const & transaction_a, btcb::pending_key const & key_a)
{
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> result (std::make_unique<btcb::mdb_merge_iterator<btcb::pending_key, btcb::pending_info>> (transaction_a, pending_v0, pending_v1, mdb_val (key_a)));
	return result;
}

btcb::store_iterator<btcb::pending_key, btcb::pending_info> btcb::mdb_store::pending_begin (btcb::transaction const & transaction_a)
{
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> result (std::make_unique<btcb::mdb_merge_iterator<btcb::pending_key, btcb::pending_info>> (transaction_a, pending_v0, pending_v1));
	return result;
}

btcb::store_iterator<btcb::pending_key, btcb::pending_info> btcb::mdb_store::pending_end ()
{
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> result (nullptr);
	return result;
}

btcb::store_iterator<btcb::pending_key, btcb::pending_info> btcb::mdb_store::pending_v0_begin (btcb::transaction const & transaction_a, btcb::pending_key const & key_a)
{
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> result (std::make_unique<btcb::mdb_iterator<btcb::pending_key, btcb::pending_info>> (transaction_a, pending_v0, mdb_val (key_a)));
	return result;
}

btcb::store_iterator<btcb::pending_key, btcb::pending_info> btcb::mdb_store::pending_v0_begin (btcb::transaction const & transaction_a)
{
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> result (std::make_unique<btcb::mdb_iterator<btcb::pending_key, btcb::pending_info>> (transaction_a, pending_v0));
	return result;
}

btcb::store_iterator<btcb::pending_key, btcb::pending_info> btcb::mdb_store::pending_v0_end ()
{
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> result (nullptr);
	return result;
}

btcb::store_iterator<btcb::pending_key, btcb::pending_info> btcb::mdb_store::pending_v1_begin (btcb::transaction const & transaction_a, btcb::pending_key const & key_a)
{
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> result (std::make_unique<btcb::mdb_iterator<btcb::pending_key, btcb::pending_info>> (transaction_a, pending_v1, mdb_val (key_a)));
	return result;
}

btcb::store_iterator<btcb::pending_key, btcb::pending_info> btcb::mdb_store::pending_v1_begin (btcb::transaction const & transaction_a)
{
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> result (std::make_unique<btcb::mdb_iterator<btcb::pending_key, btcb::pending_info>> (transaction_a, pending_v1));
	return result;
}

btcb::store_iterator<btcb::pending_key, btcb::pending_info> btcb::mdb_store::pending_v1_end ()
{
	btcb::store_iterator<btcb::pending_key, btcb::pending_info> result (nullptr);
	return result;
}

bool btcb::mdb_store::block_info_get (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a, btcb::block_info & block_info_a) const
{
	assert (!full_sideband (transaction_a));
	btcb::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), blocks_info, btcb::mdb_val (hash_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	bool result (true);
	if (status != MDB_NOTFOUND)
	{
		result = false;
		assert (value.size () == sizeof (block_info_a.account.bytes) + sizeof (block_info_a.balance.bytes));
		btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error1 (btcb::try_read (stream, block_info_a.account));
		assert (!error1);
		auto error2 (btcb::try_read (stream, block_info_a.balance));
		assert (!error2);
	}
	return result;
}

btcb::uint128_t btcb::mdb_store::representation_get (btcb::transaction const & transaction_a, btcb::account const & account_a)
{
	btcb::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), representation, btcb::mdb_val (account_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	btcb::uint128_t result = 0;
	if (status == 0)
	{
		btcb::uint128_union rep;
		btcb::bufferstream stream (reinterpret_cast<uint8_t const *> (value.data ()), value.size ());
		auto error (btcb::try_read (stream, rep));
		assert (!error);
		result = rep.number ();
	}
	return result;
}

void btcb::mdb_store::representation_put (btcb::transaction const & transaction_a, btcb::account const & account_a, btcb::uint128_t const & representation_a)
{
	btcb::uint128_union rep (representation_a);
	auto status (mdb_put (env.tx (transaction_a), representation, btcb::mdb_val (account_a), btcb::mdb_val (rep), 0));
	release_assert (status == 0);
}

void btcb::mdb_store::unchecked_clear (btcb::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), unchecked, 0));
	release_assert (status == 0);
}

void btcb::mdb_store::unchecked_put (btcb::transaction const & transaction_a, btcb::unchecked_key const & key_a, btcb::unchecked_info const & info_a)
{
	auto status (mdb_put (env.tx (transaction_a), unchecked, btcb::mdb_val (key_a), btcb::mdb_val (info_a), 0));
	release_assert (status == 0);
}

void btcb::mdb_store::unchecked_put (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a, std::shared_ptr<btcb::block> const & block_a)
{
	btcb::unchecked_key key (hash_a, block_a->hash ());
	btcb::unchecked_info info (block_a, block_a->account (), btcb::seconds_since_epoch (), btcb::signature_verification::unknown);
	unchecked_put (transaction_a, key, info);
}

std::shared_ptr<btcb::vote> btcb::mdb_store::vote_get (btcb::transaction const & transaction_a, btcb::account const & account_a)
{
	btcb::mdb_val value;
	auto status (mdb_get (env.tx (transaction_a), vote, btcb::mdb_val (account_a), value));
	release_assert (status == 0 || status == MDB_NOTFOUND);
	if (status == 0)
	{
		std::shared_ptr<btcb::vote> result (value);
		assert (result != nullptr);
		return result;
	}
	return nullptr;
}

std::vector<btcb::unchecked_info> btcb::mdb_store::unchecked_get (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a)
{
	std::vector<btcb::unchecked_info> result;
	for (auto i (unchecked_begin (transaction_a, btcb::unchecked_key (hash_a, 0))), n (unchecked_end ()); i != n && btcb::block_hash (i->first.key ()) == hash_a; ++i)
	{
		btcb::unchecked_info unchecked_info (i->second);
		result.push_back (unchecked_info);
	}
	return result;
}

void btcb::mdb_store::unchecked_del (btcb::transaction const & transaction_a, btcb::unchecked_key const & key_a)
{
	auto status (mdb_del (env.tx (transaction_a), unchecked, btcb::mdb_val (key_a), nullptr));
	release_assert (status == 0 || status == MDB_NOTFOUND);
}

size_t btcb::mdb_store::unchecked_count (btcb::transaction const & transaction_a)
{
	MDB_stat unchecked_stats;
	auto status (mdb_stat (env.tx (transaction_a), unchecked, &unchecked_stats));
	release_assert (status == 0);
	auto result (unchecked_stats.ms_entries);
	return result;
}

void btcb::mdb_store::online_weight_put (btcb::transaction const & transaction_a, uint64_t time_a, btcb::amount const & amount_a)
{
	auto status (mdb_put (env.tx (transaction_a), online_weight, btcb::mdb_val (time_a), btcb::mdb_val (amount_a), 0));
	release_assert (status == 0);
}

void btcb::mdb_store::online_weight_del (btcb::transaction const & transaction_a, uint64_t time_a)
{
	auto status (mdb_del (env.tx (transaction_a), online_weight, btcb::mdb_val (time_a), nullptr));
	release_assert (status == 0);
}

btcb::store_iterator<uint64_t, btcb::amount> btcb::mdb_store::online_weight_begin (btcb::transaction const & transaction_a)
{
	return btcb::store_iterator<uint64_t, btcb::amount> (std::make_unique<btcb::mdb_iterator<uint64_t, btcb::amount>> (transaction_a, online_weight));
}

btcb::store_iterator<uint64_t, btcb::amount> btcb::mdb_store::online_weight_end ()
{
	return btcb::store_iterator<uint64_t, btcb::amount> (nullptr);
}

size_t btcb::mdb_store::online_weight_count (btcb::transaction const & transaction_a) const
{
	MDB_stat online_weight_stats;
	auto status1 (mdb_stat (env.tx (transaction_a), online_weight, &online_weight_stats));
	release_assert (status1 == 0);
	return online_weight_stats.ms_entries;
}

void btcb::mdb_store::online_weight_clear (btcb::transaction const & transaction_a)
{
	auto status (mdb_drop (env.tx (transaction_a), online_weight, 0));
	release_assert (status == 0);
}

void btcb::mdb_store::flush (btcb::transaction const & transaction_a)
{
	{
		std::lock_guard<std::mutex> lock (cache_mutex);
		vote_cache_l1.swap (vote_cache_l2);
		vote_cache_l1.clear ();
	}
	for (auto i (vote_cache_l2.begin ()), n (vote_cache_l2.end ()); i != n; ++i)
	{
		std::vector<uint8_t> vector;
		{
			btcb::vectorstream stream (vector);
			i->second->serialize (stream);
		}
		auto status1 (mdb_put (env.tx (transaction_a), vote, btcb::mdb_val (i->first), btcb::mdb_val (vector.size (), vector.data ()), 0));
		release_assert (status1 == 0);
	}
}
std::shared_ptr<btcb::vote> btcb::mdb_store::vote_current (btcb::transaction const & transaction_a, btcb::account const & account_a)
{
	assert (!cache_mutex.try_lock ());
	std::shared_ptr<btcb::vote> result;
	auto existing (vote_cache_l1.find (account_a));
	auto have_existing (true);
	if (existing == vote_cache_l1.end ())
	{
		existing = vote_cache_l2.find (account_a);
		if (existing == vote_cache_l2.end ())
		{
			have_existing = false;
		}
	}
	if (have_existing)
	{
		result = existing->second;
	}
	else
	{
		result = vote_get (transaction_a, account_a);
	}
	return result;
}

std::shared_ptr<btcb::vote> btcb::mdb_store::vote_generate (btcb::transaction const & transaction_a, btcb::account const & account_a, btcb::raw_key const & key_a, std::shared_ptr<btcb::block> block_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto result (vote_current (transaction_a, account_a));
	uint64_t sequence ((result ? result->sequence : 0) + 1);
	result = std::make_shared<btcb::vote> (account_a, key_a, sequence, block_a);
	vote_cache_l1[account_a] = result;
	return result;
}

std::shared_ptr<btcb::vote> btcb::mdb_store::vote_generate (btcb::transaction const & transaction_a, btcb::account const & account_a, btcb::raw_key const & key_a, std::vector<btcb::block_hash> blocks_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto result (vote_current (transaction_a, account_a));
	uint64_t sequence ((result ? result->sequence : 0) + 1);
	result = std::make_shared<btcb::vote> (account_a, key_a, sequence, blocks_a);
	vote_cache_l1[account_a] = result;
	return result;
}

std::shared_ptr<btcb::vote> btcb::mdb_store::vote_max (btcb::transaction const & transaction_a, std::shared_ptr<btcb::vote> vote_a)
{
	std::lock_guard<std::mutex> lock (cache_mutex);
	auto current (vote_current (transaction_a, vote_a->account));
	auto result (vote_a);
	if (current != nullptr && current->sequence > result->sequence)
	{
		result = current;
	}
	vote_cache_l1[vote_a->account] = result;
	return result;
}

btcb::store_iterator<btcb::account, btcb::account_info> btcb::mdb_store::latest_begin (btcb::transaction const & transaction_a, btcb::account const & account_a)
{
	btcb::store_iterator<btcb::account, btcb::account_info> result (std::make_unique<btcb::mdb_merge_iterator<btcb::account, btcb::account_info>> (transaction_a, accounts_v0, accounts_v1, btcb::mdb_val (account_a)));
	return result;
}

btcb::store_iterator<btcb::account, btcb::account_info> btcb::mdb_store::latest_begin (btcb::transaction const & transaction_a)
{
	btcb::store_iterator<btcb::account, btcb::account_info> result (std::make_unique<btcb::mdb_merge_iterator<btcb::account, btcb::account_info>> (transaction_a, accounts_v0, accounts_v1));
	return result;
}

btcb::store_iterator<btcb::account, btcb::account_info> btcb::mdb_store::latest_end ()
{
	btcb::store_iterator<btcb::account, btcb::account_info> result (nullptr);
	return result;
}

btcb::store_iterator<btcb::account, btcb::account_info> btcb::mdb_store::latest_v0_begin (btcb::transaction const & transaction_a, btcb::account const & account_a)
{
	btcb::store_iterator<btcb::account, btcb::account_info> result (std::make_unique<btcb::mdb_iterator<btcb::account, btcb::account_info>> (transaction_a, accounts_v0, btcb::mdb_val (account_a)));
	return result;
}

btcb::store_iterator<btcb::account, btcb::account_info> btcb::mdb_store::latest_v0_begin (btcb::transaction const & transaction_a)
{
	btcb::store_iterator<btcb::account, btcb::account_info> result (std::make_unique<btcb::mdb_iterator<btcb::account, btcb::account_info>> (transaction_a, accounts_v0));
	return result;
}

btcb::store_iterator<btcb::account, btcb::account_info> btcb::mdb_store::latest_v0_end ()
{
	btcb::store_iterator<btcb::account, btcb::account_info> result (nullptr);
	return result;
}

btcb::store_iterator<btcb::account, btcb::account_info> btcb::mdb_store::latest_v1_begin (btcb::transaction const & transaction_a, btcb::account const & account_a)
{
	btcb::store_iterator<btcb::account, btcb::account_info> result (std::make_unique<btcb::mdb_iterator<btcb::account, btcb::account_info>> (transaction_a, accounts_v1, btcb::mdb_val (account_a)));
	return result;
}

btcb::store_iterator<btcb::account, btcb::account_info> btcb::mdb_store::latest_v1_begin (btcb::transaction const & transaction_a)
{
	btcb::store_iterator<btcb::account, btcb::account_info> result (std::make_unique<btcb::mdb_iterator<btcb::account, btcb::account_info>> (transaction_a, accounts_v1));
	return result;
}

btcb::store_iterator<btcb::account, btcb::account_info> btcb::mdb_store::latest_v1_end ()
{
	btcb::store_iterator<btcb::account, btcb::account_info> result (nullptr);
	return result;
}
