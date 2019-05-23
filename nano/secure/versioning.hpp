#pragma once

#include <btcb/lib/blocks.hpp>
#include <btcb/node/lmdb.hpp>
#include <btcb/secure/utility.hpp>

namespace btcb
{
class account_info_v1 final
{
public:
	account_info_v1 () = default;
	explicit account_info_v1 (MDB_val const &);
	account_info_v1 (btcb::block_hash const &, btcb::block_hash const &, btcb::amount const &, uint64_t);
	btcb::mdb_val val () const;
	btcb::block_hash head{ 0 };
	btcb::block_hash rep_block{ 0 };
	btcb::amount balance{ 0 };
	uint64_t modified{ 0 };
};
class pending_info_v3 final
{
public:
	pending_info_v3 () = default;
	explicit pending_info_v3 (MDB_val const &);
	pending_info_v3 (btcb::account const &, btcb::amount const &, btcb::account const &);
	btcb::mdb_val val () const;
	btcb::account source{ 0 };
	btcb::amount amount{ 0 };
	btcb::account destination{ 0 };
};
class account_info_v5 final
{
public:
	account_info_v5 () = default;
	explicit account_info_v5 (MDB_val const &);
	account_info_v5 (btcb::block_hash const &, btcb::block_hash const &, btcb::block_hash const &, btcb::amount const &, uint64_t);
	btcb::mdb_val val () const;
	btcb::block_hash head{ 0 };
	btcb::block_hash rep_block{ 0 };
	btcb::block_hash open_block{ 0 };
	btcb::amount balance{ 0 };
	uint64_t modified{ 0 };
};
class account_info_v13 final
{
public:
	account_info_v13 () = default;
	account_info_v13 (btcb::block_hash const &, btcb::block_hash const &, btcb::block_hash const &, btcb::amount const &, uint64_t, uint64_t block_count, btcb::epoch epoch_a);
	size_t db_size () const;
	btcb::block_hash head{ 0 };
	btcb::block_hash rep_block{ 0 };
	btcb::block_hash open_block{ 0 };
	btcb::amount balance{ 0 };
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	btcb::epoch epoch{ btcb::epoch::epoch_0 };
};
}
