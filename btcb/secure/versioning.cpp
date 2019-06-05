#include <btcb/secure/versioning.hpp>

btcb::account_info_v1::account_info_v1 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (head) + sizeof (rep_block) + sizeof (balance) + sizeof (modified) == sizeof (*this), "Class not packed");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

btcb::account_info_v1::account_info_v1 (btcb::block_hash const & head_a, btcb::block_hash const & rep_block_a, btcb::amount const & balance_a, uint64_t modified_a) :
head (head_a),
rep_block (rep_block_a),
balance (balance_a),
modified (modified_a)
{
}

btcb::mdb_val btcb::account_info_v1::val () const
{
	return btcb::mdb_val (sizeof (*this), const_cast<btcb::account_info_v1 *> (this));
}

btcb::pending_info_v3::pending_info_v3 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (source) + sizeof (amount) + sizeof (destination) == sizeof (*this), "Packed class");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

btcb::pending_info_v3::pending_info_v3 (btcb::account const & source_a, btcb::amount const & amount_a, btcb::account const & destination_a) :
source (source_a),
amount (amount_a),
destination (destination_a)
{
}

btcb::mdb_val btcb::pending_info_v3::val () const
{
	return btcb::mdb_val (sizeof (*this), const_cast<btcb::pending_info_v3 *> (this));
}

btcb::account_info_v5::account_info_v5 (MDB_val const & val_a)
{
	assert (val_a.mv_size == sizeof (*this));
	static_assert (sizeof (head) + sizeof (rep_block) + sizeof (open_block) + sizeof (balance) + sizeof (modified) == sizeof (*this), "Class not packed");
	std::copy (reinterpret_cast<uint8_t const *> (val_a.mv_data), reinterpret_cast<uint8_t const *> (val_a.mv_data) + sizeof (*this), reinterpret_cast<uint8_t *> (this));
}

btcb::account_info_v5::account_info_v5 (btcb::block_hash const & head_a, btcb::block_hash const & rep_block_a, btcb::block_hash const & open_block_a, btcb::amount const & balance_a, uint64_t modified_a) :
head (head_a),
rep_block (rep_block_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a)
{
}

btcb::mdb_val btcb::account_info_v5::val () const
{
	return btcb::mdb_val (sizeof (*this), const_cast<btcb::account_info_v5 *> (this));
}

btcb::account_info_v13::account_info_v13 (btcb::block_hash const & head_a, btcb::block_hash const & rep_block_a, btcb::block_hash const & open_block_a, btcb::amount const & balance_a, uint64_t modified_a, uint64_t block_count_a, btcb::epoch epoch_a) :
head (head_a),
rep_block (rep_block_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a),
block_count (block_count_a),
epoch (epoch_a)
{
}

size_t btcb::account_info_v13::db_size () const
{
	assert (reinterpret_cast<const uint8_t *> (this) == reinterpret_cast<const uint8_t *> (&head));
	assert (reinterpret_cast<const uint8_t *> (&head) + sizeof (head) == reinterpret_cast<const uint8_t *> (&rep_block));
	assert (reinterpret_cast<const uint8_t *> (&rep_block) + sizeof (rep_block) == reinterpret_cast<const uint8_t *> (&open_block));
	assert (reinterpret_cast<const uint8_t *> (&open_block) + sizeof (open_block) == reinterpret_cast<const uint8_t *> (&balance));
	assert (reinterpret_cast<const uint8_t *> (&balance) + sizeof (balance) == reinterpret_cast<const uint8_t *> (&modified));
	assert (reinterpret_cast<const uint8_t *> (&modified) + sizeof (modified) == reinterpret_cast<const uint8_t *> (&block_count));
	return sizeof (head) + sizeof (rep_block) + sizeof (open_block) + sizeof (balance) + sizeof (modified) + sizeof (block_count);
}
