#include <crypto/cryptopp/filters.h>
#include <crypto/cryptopp/randpool.h>
#include <gtest/gtest.h>
#include <btcb/core_test/testutil.hpp>
#include <btcb/node/stats.hpp>
#include <btcb/node/testing.hpp>

using namespace std::chrono_literals;

// Init returns an error if it can't open files at the path
TEST (ledger, store_error)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, boost::filesystem::path ("///"));
	ASSERT_FALSE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
}

// Ledger can be initialized and returns a basic query for an empty account
TEST (ledger, empty)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::account account;
	auto transaction (store.tx_begin_read ());
	auto balance (ledger.account_balance (transaction, account));
	ASSERT_TRUE (balance.is_zero ());
}

// Genesis account should have the max balance on empty initialization
TEST (ledger, genesis_balance)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	auto balance (ledger.account_balance (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount, balance);
	auto amount (ledger.amount (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount, amount);
	btcb::account_info info;
	ASSERT_FALSE (store.account_get (transaction, btcb::genesis_account, info));
	// Frontier time should have been updated when genesis balance was added
	ASSERT_GE (btcb::seconds_since_epoch (), info.modified);
	ASSERT_LT (btcb::seconds_since_epoch () - info.modified, 10);
	// Genesis block should be confirmed by default
	ASSERT_EQ (info.confirmation_height, 1);
}

// All nodes in the system should agree on the genesis balance
TEST (system, system_genesis)
{
	btcb::system system (24000, 2);
	for (auto & i : system.nodes)
	{
		auto transaction (i->store.tx_begin_read ());
		ASSERT_EQ (btcb::genesis_amount, i->ledger.account_balance (transaction, btcb::genesis_account));
	}
}

// Create a send block and publish it.
TEST (ledger, process_send)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	auto transaction (store.tx_begin_write ());
	btcb::genesis genesis;
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
	btcb::keypair key2;
	btcb::send_block send (info1.head, key2.pub, 50, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
	btcb::block_hash hash1 (send.hash ());
	ASSERT_EQ (btcb::test_genesis_key.pub, store.frontier_get (transaction, info1.head));
	ASSERT_EQ (1, info1.block_count);
	// This was a valid block, it should progress.
	auto return1 (ledger.process (transaction, send));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.amount (transaction, hash1));
	ASSERT_TRUE (store.frontier_get (transaction, info1.head).is_zero ());
	ASSERT_EQ (btcb::test_genesis_key.pub, store.frontier_get (transaction, hash1));
	ASSERT_EQ (btcb::process_result::progress, return1.code);
	ASSERT_EQ (btcb::test_genesis_key.pub, return1.account);
	ASSERT_EQ (btcb::genesis_amount - 50, return1.amount.number ());
	ASSERT_EQ (50, ledger.account_balance (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.account_pending (transaction, key2.pub));
	btcb::account_info info2;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info2));
	ASSERT_EQ (2, info2.block_count);
	auto latest6 (store.block_get (transaction, info2.head));
	ASSERT_NE (nullptr, latest6);
	auto latest7 (dynamic_cast<btcb::send_block *> (latest6.get ()));
	ASSERT_NE (nullptr, latest7);
	ASSERT_EQ (send, *latest7);
	// Create an open block opening an account accepting the send we just created
	btcb::open_block open (hash1, key2.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	btcb::block_hash hash2 (open.hash ());
	// This was a valid block, it should progress.
	auto return2 (ledger.process (transaction, open));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.amount (transaction, hash2));
	ASSERT_EQ (btcb::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, return2.account);
	ASSERT_EQ (btcb::genesis_amount - 50, return2.amount.number ());
	ASSERT_EQ (key2.pub, store.frontier_get (transaction, hash2));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.weight (transaction, key2.pub));
	btcb::account_info info3;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info3));
	auto latest2 (store.block_get (transaction, info3.head));
	ASSERT_NE (nullptr, latest2);
	auto latest3 (dynamic_cast<btcb::send_block *> (latest2.get ()));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (send, *latest3);
	btcb::account_info info4;
	ASSERT_FALSE (store.account_get (transaction, key2.pub, info4));
	auto latest4 (store.block_get (transaction, info4.head));
	ASSERT_NE (nullptr, latest4);
	auto latest5 (dynamic_cast<btcb::open_block *> (latest4.get ()));
	ASSERT_NE (nullptr, latest5);
	ASSERT_EQ (open, *latest5);
	ASSERT_FALSE (ledger.rollback (transaction, hash2));
	ASSERT_TRUE (store.frontier_get (transaction, hash2).is_zero ());
	btcb::account_info info5;
	ASSERT_TRUE (ledger.store.account_get (transaction, key2.pub, info5));
	btcb::pending_info pending1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, btcb::pending_key (key2.pub, hash1), pending1));
	ASSERT_EQ (btcb::test_genesis_key.pub, pending1.source);
	ASSERT_EQ (btcb::genesis_amount - 50, pending1.amount.number ());
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (50, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	btcb::account_info info6;
	ASSERT_FALSE (ledger.store.account_get (transaction, btcb::test_genesis_key.pub, info6));
	ASSERT_EQ (hash1, info6.head);
	ASSERT_FALSE (ledger.rollback (transaction, info6.head));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (btcb::test_genesis_key.pub, store.frontier_get (transaction, info1.head));
	ASSERT_TRUE (store.frontier_get (transaction, hash1).is_zero ());
	btcb::account_info info7;
	ASSERT_FALSE (ledger.store.account_get (transaction, btcb::test_genesis_key.pub, info7));
	ASSERT_EQ (1, info7.block_count);
	ASSERT_EQ (info1.head, info7.head);
	btcb::pending_info pending2;
	ASSERT_TRUE (ledger.store.pending_get (transaction, btcb::pending_key (key2.pub, hash1), pending2));
	ASSERT_EQ (btcb::genesis_amount, ledger.account_balance (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
}

TEST (ledger, process_receive)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
	btcb::keypair key2;
	btcb::send_block send (info1.head, key2.pub, 50, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
	btcb::block_hash hash1 (send.hash ());
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send).code);
	btcb::keypair key3;
	btcb::open_block open (hash1, key3.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	btcb::block_hash hash2 (open.hash ());
	auto return1 (ledger.process (transaction, open));
	ASSERT_EQ (btcb::process_result::progress, return1.code);
	ASSERT_EQ (key2.pub, return1.account);
	ASSERT_EQ (btcb::genesis_amount - 50, return1.amount.number ());
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.weight (transaction, key3.pub));
	btcb::send_block send2 (hash1, key2.pub, 25, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (hash1));
	btcb::block_hash hash3 (send2.hash ());
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send2).code);
	btcb::receive_block receive (hash2, hash3, key2.prv, key2.pub, pool.generate (hash2));
	auto hash4 (receive.hash ());
	ASSERT_EQ (key2.pub, store.frontier_get (transaction, hash2));
	auto return2 (ledger.process (transaction, receive));
	ASSERT_EQ (25, ledger.amount (transaction, hash4));
	ASSERT_TRUE (store.frontier_get (transaction, hash2).is_zero ());
	ASSERT_EQ (key2.pub, store.frontier_get (transaction, hash4));
	ASSERT_EQ (btcb::process_result::progress, return2.code);
	ASSERT_EQ (key2.pub, return2.account);
	ASSERT_EQ (25, return2.amount.number ());
	ASSERT_EQ (hash4, ledger.latest (transaction, key2.pub));
	ASSERT_EQ (25, ledger.account_balance (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (btcb::genesis_amount - 25, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (btcb::genesis_amount - 25, ledger.weight (transaction, key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, hash4));
	ASSERT_TRUE (store.block_successor (transaction, hash2).is_zero ());
	ASSERT_EQ (key2.pub, store.frontier_get (transaction, hash2));
	ASSERT_TRUE (store.frontier_get (transaction, hash4).is_zero ());
	ASSERT_EQ (25, ledger.account_balance (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (25, ledger.account_pending (transaction, key2.pub));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (hash2, ledger.latest (transaction, key2.pub));
	btcb::pending_info pending1;
	ASSERT_FALSE (ledger.store.pending_get (transaction, btcb::pending_key (key2.pub, hash3), pending1));
	ASSERT_EQ (btcb::test_genesis_key.pub, pending1.source);
	ASSERT_EQ (25, pending1.amount.number ());
}

TEST (ledger, rollback_receiver)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
	btcb::keypair key2;
	btcb::send_block send (info1.head, key2.pub, 50, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
	btcb::block_hash hash1 (send.hash ());
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send).code);
	btcb::keypair key3;
	btcb::open_block open (hash1, key3.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	btcb::block_hash hash2 (open.hash ());
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (hash2, ledger.latest (transaction, key2.pub));
	ASSERT_EQ (50, ledger.account_balance (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (50, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.weight (transaction, key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, hash1));
	ASSERT_EQ (btcb::genesis_amount, ledger.account_balance (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.account_balance (transaction, key2.pub));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	btcb::account_info info2;
	ASSERT_TRUE (ledger.store.account_get (transaction, key2.pub, info2));
	btcb::pending_info pending1;
	ASSERT_TRUE (ledger.store.pending_get (transaction, btcb::pending_key (key2.pub, info2.head), pending1));
}

TEST (ledger, rollback_representation)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key5;
	btcb::change_block change1 (genesis.hash (), key5.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, change1).code);
	btcb::keypair key3;
	btcb::change_block change2 (change1.hash (), key3.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (change1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, change2).code);
	btcb::keypair key2;
	btcb::send_block send1 (change2.hash (), key2.pub, 50, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (change2.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::keypair key4;
	btcb::open_block open (send1.hash (), key4.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open).code);
	btcb::send_block send2 (send1.hash (), key2.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send2).code);
	btcb::receive_block receive1 (open.hash (), send2.hash (), key2.prv, key2.pub, pool.generate (open.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_EQ (1, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (btcb::genesis_amount - 1, ledger.weight (transaction, key4.pub));
	btcb::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, key2.pub, info1));
	ASSERT_EQ (open.hash (), info1.rep_block);
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	btcb::account_info info2;
	ASSERT_FALSE (store.account_get (transaction, key2.pub, info2));
	ASSERT_EQ (open.hash (), info2.rep_block);
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.weight (transaction, key4.pub));
	ASSERT_FALSE (ledger.rollback (transaction, open.hash ()));
	ASSERT_EQ (1, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key4.pub));
	ledger.rollback (transaction, send1.hash ());
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, key3.pub));
	btcb::account_info info3;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info3));
	ASSERT_EQ (change2.hash (), info3.rep_block);
	ASSERT_FALSE (ledger.rollback (transaction, change2.hash ()));
	btcb::account_info info4;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info4));
	ASSERT_EQ (change1.hash (), info4.rep_block);
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, key5.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
}

TEST (ledger, receive_rollback)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::send_block send (genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send).code);
	btcb::receive_block receive (send.hash (), send.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive).code);
	ASSERT_FALSE (ledger.rollback (transaction, receive.hash ()));
}

TEST (ledger, process_duplicate)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
	btcb::keypair key2;
	btcb::send_block send (info1.head, key2.pub, 50, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
	btcb::block_hash hash1 (send.hash ());
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (btcb::process_result::old, ledger.process (transaction, send).code);
	btcb::open_block open (hash1, 1, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (btcb::process_result::old, ledger.process (transaction, open).code);
}

TEST (ledger, representative_genesis)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	auto latest (ledger.latest (transaction, btcb::test_genesis_key.pub));
	ASSERT_FALSE (latest.is_zero ());
	ASSERT_EQ (genesis.open->hash (), ledger.representative (transaction, latest));
}

TEST (ledger, weight)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::genesis_account));
}

TEST (ledger, representative_change)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::keypair key2;
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	btcb::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
	btcb::change_block block (info1.head, key2.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
	ASSERT_EQ (btcb::test_genesis_key.pub, store.frontier_get (transaction, info1.head));
	auto return1 (ledger.process (transaction, block));
	ASSERT_EQ (0, ledger.amount (transaction, block.hash ()));
	ASSERT_TRUE (store.frontier_get (transaction, info1.head).is_zero ());
	ASSERT_EQ (btcb::test_genesis_key.pub, store.frontier_get (transaction, block.hash ()));
	ASSERT_EQ (btcb::process_result::progress, return1.code);
	ASSERT_EQ (btcb::test_genesis_key.pub, return1.account);
	ASSERT_EQ (0, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, key2.pub));
	btcb::account_info info2;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info2));
	ASSERT_EQ (block.hash (), info2.head);
	ASSERT_FALSE (ledger.rollback (transaction, info2.head));
	ASSERT_EQ (btcb::test_genesis_key.pub, store.frontier_get (transaction, info1.head));
	ASSERT_TRUE (store.frontier_get (transaction, block.hash ()).is_zero ());
	btcb::account_info info3;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info3));
	ASSERT_EQ (info1.head, info3.head);
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
}

TEST (ledger, send_fork)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::keypair key2;
	btcb::keypair key3;
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
	btcb::send_block block (info1.head, key2.pub, 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block).code);
	btcb::send_block block2 (info1.head, key3.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
	ASSERT_EQ (btcb::process_result::fork, ledger.process (transaction, block2).code);
}

TEST (ledger, receive_fork)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::keypair key2;
	btcb::keypair key3;
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
	btcb::send_block block (info1.head, key2.pub, 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block).code);
	btcb::open_block block2 (block.hash (), key2.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block2).code);
	btcb::change_block block3 (block2.hash (), key3.pub, key2.prv, key2.pub, pool.generate (block2.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block3).code);
	btcb::send_block block4 (block.hash (), key2.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block4).code);
	btcb::receive_block block5 (block2.hash (), block4.hash (), key2.prv, key2.pub, pool.generate (block2.hash ()));
	ASSERT_EQ (btcb::process_result::fork, ledger.process (transaction, block5).code);
}

TEST (ledger, open_fork)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::keypair key2;
	btcb::keypair key3;
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
	btcb::send_block block (info1.head, key2.pub, 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block).code);
	btcb::open_block block2 (block.hash (), key2.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block2).code);
	btcb::open_block block3 (block.hash (), key3.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (btcb::process_result::fork, ledger.process (transaction, block3).code);
}

TEST (system, DISABLED_generate_send_existing)
{
	btcb::system system (24000, 1);
	btcb::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (btcb::genesis_account, stake_preserver.pub, btcb::genesis_amount / 3 * 2, true));
	btcb::account_info info1;
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, btcb::test_genesis_key.pub, info1));
	}
	std::vector<btcb::account> accounts;
	accounts.push_back (btcb::test_genesis_key.pub);
	system.generate_send_existing (*system.nodes[0], accounts);
	// Have stake_preserver receive funds after generate_send_existing so it isn't chosen as the destination
	{
		auto transaction (system.nodes[0]->store.tx_begin_write ());
		auto open_block (std::make_shared<btcb::open_block> (send_block->hash (), btcb::genesis_account, stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		system.nodes[0]->work_generate_blocking (*open_block);
		ASSERT_EQ (btcb::process_result::progress, system.nodes[0]->ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (system.nodes[0]->balance (stake_preserver.pub), system.nodes[0]->balance (btcb::genesis_account));
	btcb::account_info info2;
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, btcb::test_genesis_key.pub, info2));
	}
	ASSERT_NE (info1.head, info2.head);
	system.deadline_set (15s);
	while (info2.block_count < info1.block_count + 2)
	{
		ASSERT_NO_ERROR (system.poll ());
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_FALSE (system.nodes[0]->store.account_get (transaction, btcb::test_genesis_key.pub, info2));
	}
	ASSERT_EQ (info1.block_count + 2, info2.block_count);
	ASSERT_EQ (info2.balance, btcb::genesis_amount / 3);
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_NE (system.nodes[0]->ledger.amount (transaction, info2.head), 0);
	}
	system.stop ();
	runner.join ();
}

TEST (system, generate_send_new)
{
	btcb::system system (24000, 1);
	btcb::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		auto iterator1 (system.nodes[0]->store.latest_begin (transaction));
		ASSERT_NE (system.nodes[0]->store.latest_end (), iterator1);
		++iterator1;
		ASSERT_EQ (system.nodes[0]->store.latest_end (), iterator1);
	}
	btcb::keypair stake_preserver;
	auto send_block (system.wallet (0)->send_action (btcb::genesis_account, stake_preserver.pub, btcb::genesis_amount / 3 * 2, true));
	{
		auto transaction (system.nodes[0]->store.tx_begin_write ());
		auto open_block (std::make_shared<btcb::open_block> (send_block->hash (), btcb::genesis_account, stake_preserver.pub, stake_preserver.prv, stake_preserver.pub, 0));
		system.nodes[0]->work_generate_blocking (*open_block);
		ASSERT_EQ (btcb::process_result::progress, system.nodes[0]->ledger.process (transaction, *open_block).code);
	}
	ASSERT_GT (system.nodes[0]->balance (stake_preserver.pub), system.nodes[0]->balance (btcb::genesis_account));
	std::vector<btcb::account> accounts;
	accounts.push_back (btcb::test_genesis_key.pub);
	system.generate_send_new (*system.nodes[0], accounts);
	btcb::account new_account (0);
	{
		auto transaction (system.nodes[0]->wallets.tx_begin_read ());
		auto iterator2 (system.wallet (0)->store.begin (transaction));
		if (btcb::uint256_union (iterator2->first) != btcb::test_genesis_key.pub)
		{
			new_account = btcb::uint256_union (iterator2->first);
		}
		++iterator2;
		ASSERT_NE (system.wallet (0)->store.end (), iterator2);
		if (btcb::uint256_union (iterator2->first) != btcb::test_genesis_key.pub)
		{
			new_account = btcb::uint256_union (iterator2->first);
		}
		++iterator2;
		ASSERT_EQ (system.wallet (0)->store.end (), iterator2);
		ASSERT_FALSE (new_account.is_zero ());
	}
	system.deadline_set (10s);
	while (system.nodes[0]->balance (new_account) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.stop ();
	runner.join ();
}

TEST (ledger, representation)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (btcb::genesis_amount, store.representation_get (transaction, btcb::test_genesis_key.pub));
	btcb::keypair key2;
	btcb::send_block block1 (genesis.hash (), key2.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
	ASSERT_EQ (btcb::genesis_amount - 100, store.representation_get (transaction, btcb::test_genesis_key.pub));
	btcb::keypair key3;
	btcb::open_block block2 (block1.hash (), key3.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (btcb::genesis_amount - 100, store.representation_get (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key3.pub));
	btcb::send_block block3 (block1.hash (), key2.pub, btcb::genesis_amount - 200, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block3).code);
	ASSERT_EQ (btcb::genesis_amount - 200, store.representation_get (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key3.pub));
	btcb::receive_block block4 (block2.hash (), block3.hash (), key2.prv, key2.pub, pool.generate (block2.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (btcb::genesis_amount - 200, store.representation_get (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (200, store.representation_get (transaction, key3.pub));
	btcb::keypair key4;
	btcb::change_block block5 (block4.hash (), key4.pub, key2.prv, key2.pub, pool.generate (block4.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block5).code);
	ASSERT_EQ (btcb::genesis_amount - 200, store.representation_get (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key3.pub));
	ASSERT_EQ (200, store.representation_get (transaction, key4.pub));
	btcb::keypair key5;
	btcb::send_block block6 (block5.hash (), key5.pub, 100, key2.prv, key2.pub, pool.generate (block5.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block6).code);
	ASSERT_EQ (btcb::genesis_amount - 200, store.representation_get (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key3.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key5.pub));
	btcb::keypair key6;
	btcb::open_block block7 (block6.hash (), key6.pub, key5.pub, key5.prv, key5.pub, pool.generate (key5.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block7).code);
	ASSERT_EQ (btcb::genesis_amount - 200, store.representation_get (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key3.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key5.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key6.pub));
	btcb::send_block block8 (block6.hash (), key5.pub, 0, key2.prv, key2.pub, pool.generate (block6.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block8).code);
	ASSERT_EQ (btcb::genesis_amount - 200, store.representation_get (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key3.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key5.pub));
	ASSERT_EQ (100, store.representation_get (transaction, key6.pub));
	btcb::receive_block block9 (block7.hash (), block8.hash (), key5.prv, key5.pub, pool.generate (block7.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block9).code);
	ASSERT_EQ (btcb::genesis_amount - 200, store.representation_get (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key2.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key3.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key4.pub));
	ASSERT_EQ (0, store.representation_get (transaction, key5.pub));
	ASSERT_EQ (200, store.representation_get (transaction, key6.pub));
}

TEST (ledger, double_open)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key2;
	btcb::send_block send1 (genesis.hash (), key2.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::open_block open1 (send1.hash (), key2.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	btcb::open_block open2 (send1.hash (), btcb::test_genesis_key.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (btcb::process_result::fork, ledger.process (transaction, open2).code);
}

TEST (ledger, double_receive)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key2;
	btcb::send_block send1 (genesis.hash (), key2.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::open_block open1 (send1.hash (), key2.pub, key2.pub, key2.prv, key2.pub, pool.generate (key2.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	btcb::receive_block receive1 (open1.hash (), send1.hash (), key2.prv, key2.pub, pool.generate (open1.hash ()));
	ASSERT_EQ (btcb::process_result::unreceivable, ledger.process (transaction, receive1).code);
}

TEST (votes, check_signature)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair key1;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (btcb::process_result::progress, node1.ledger.process (transaction, *send1).code);
	auto node_l (system.nodes[0]);
	node1.active.start (send1);
	std::lock_guard<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	ASSERT_EQ (1, votes1->last_votes.size ());
	auto vote1 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 1, send1));
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (btcb::vote_code::invalid, node1.vote_processor.vote_blocking (transaction, vote1, std::make_shared<btcb::transport::channel_udp> (node1.network.udp_channels, btcb::endpoint (boost::asio::ip::address_v6 (), 0))));
	vote1->signature.bytes[0] ^= 1;
	ASSERT_EQ (btcb::vote_code::vote, node1.vote_processor.vote_blocking (transaction, vote1, std::make_shared<btcb::transport::channel_udp> (node1.network.udp_channels, btcb::endpoint (boost::asio::ip::address_v6 (), 0))));
	ASSERT_EQ (btcb::vote_code::replay, node1.vote_processor.vote_blocking (transaction, vote1, std::make_shared<btcb::transport::channel_udp> (node1.network.udp_channels, btcb::endpoint (boost::asio::ip::address_v6 (), 0))));
}

TEST (votes, add_one)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair key1;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (btcb::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.active.start (send1);
	std::unique_lock<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	ASSERT_EQ (1, votes1->last_votes.size ());
	lock.unlock ();
	auto vote1 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 1, send1));
	ASSERT_FALSE (node1.active.vote (vote1));
	auto vote2 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 2, send1));
	ASSERT_FALSE (node1.active.vote (vote2));
	lock.lock ();
	ASSERT_EQ (2, votes1->last_votes.size ());
	auto existing1 (votes1->last_votes.find (btcb::test_genesis_key.pub));
	ASSERT_NE (votes1->last_votes.end (), existing1);
	ASSERT_EQ (send1->hash (), existing1->second.hash);
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (btcb::genesis_amount - 100, winner.first);
}

TEST (votes, add_two)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair key1;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (btcb::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.active.start (send1);
	auto vote1 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 1, send1));
	ASSERT_FALSE (node1.active.vote (vote1));
	btcb::keypair key2;
	auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	auto vote2 (std::make_shared<btcb::vote> (key2.pub, key2.prv, 1, send2));
	ASSERT_FALSE (node1.active.vote (vote2));
	{
		std::lock_guard<std::mutex> lock (node1.active.mutex);
		auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
		ASSERT_EQ (3, votes1->last_votes.size ());
		ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (btcb::test_genesis_key.pub));
		ASSERT_EQ (send1->hash (), votes1->last_votes[btcb::test_genesis_key.pub].hash);
		ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (key2.pub));
		ASSERT_EQ (send2->hash (), votes1->last_votes[key2.pub].hash);
		auto winner (*votes1->tally (transaction).begin ());
		ASSERT_EQ (*send1, *winner.second);
	}
}

// Higher sequence numbers change the vote
TEST (votes, add_existing)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair key1;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (btcb::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.active.start (send1);
	auto vote1 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 1, send1));
	ASSERT_FALSE (node1.active.vote (vote1));
	ASSERT_FALSE (node1.active.publish (send1));
	std::unique_lock<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	ASSERT_EQ (1, votes1->last_votes[btcb::test_genesis_key.pub].sequence);
	btcb::keypair key2;
	auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 2, send2));
	// Pretend we've waited the timeout
	votes1->last_votes[btcb::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	lock.unlock ();
	ASSERT_FALSE (node1.active.vote (vote2));
	ASSERT_FALSE (node1.active.publish (send2));
	lock.lock ();
	ASSERT_EQ (2, votes1->last_votes[btcb::test_genesis_key.pub].sequence);
	// Also resend the old vote, and see if we respect the sequence number
	votes1->last_votes[btcb::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	lock.unlock ();
	ASSERT_TRUE (node1.active.vote (vote1));
	lock.lock ();
	ASSERT_EQ (2, votes1->last_votes[btcb::test_genesis_key.pub].sequence);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (btcb::test_genesis_key.pub));
	ASSERT_EQ (send2->hash (), votes1->last_votes[btcb::test_genesis_key.pub].hash);
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send2, *winner.second);
}

// Lower sequence numbers are ignored
TEST (votes, add_old)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair key1;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (btcb::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.active.start (send1);
	auto vote1 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 2, send1));
	std::lock_guard<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	auto channel (std::make_shared<btcb::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint ()));
	node1.vote_processor.vote_blocking (transaction, vote1, channel);
	btcb::keypair key2;
	auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 1, send2));
	votes1->last_votes[btcb::test_genesis_key.pub].time = std::chrono::steady_clock::now () - std::chrono::seconds (20);
	node1.vote_processor.vote_blocking (transaction, vote2, channel);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (btcb::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[btcb::test_genesis_key.pub].hash);
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Lower sequence numbers are accepted for different accounts
TEST (votes, add_old_different_account)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair key1;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<btcb::send_block> (send1->hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (btcb::process_result::progress, node1.ledger.process (transaction, *send1).code);
	ASSERT_EQ (btcb::process_result::progress, node1.ledger.process (transaction, *send2).code);
	node1.active.start (send1);
	node1.active.start (send2);
	std::unique_lock<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	auto votes2 (node1.active.roots.find (send2->qualified_root ())->election);
	ASSERT_EQ (1, votes1->last_votes.size ());
	ASSERT_EQ (1, votes2->last_votes.size ());
	auto vote1 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 2, send1));
	auto channel (std::make_shared<btcb::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint ()));
	auto vote_result1 (node1.vote_processor.vote_blocking (transaction, vote1, channel));
	ASSERT_EQ (btcb::vote_code::vote, vote_result1);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_EQ (1, votes2->last_votes.size ());
	auto vote2 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 1, send2));
	auto vote_result2 (node1.vote_processor.vote_blocking (transaction, vote2, channel));
	ASSERT_EQ (btcb::vote_code::vote, vote_result2);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_EQ (2, votes2->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (btcb::test_genesis_key.pub));
	ASSERT_NE (votes2->last_votes.end (), votes2->last_votes.find (btcb::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[btcb::test_genesis_key.pub].hash);
	ASSERT_EQ (send2->hash (), votes2->last_votes[btcb::test_genesis_key.pub].hash);
	auto winner1 (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner1.second);
	auto winner2 (*votes2->tally (transaction).begin ());
	ASSERT_EQ (*send2, *winner2.second);
}

// The voting cooldown is respected
TEST (votes, add_cooldown)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair key1;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto transaction (node1.store.tx_begin_write ());
	ASSERT_EQ (btcb::process_result::progress, node1.ledger.process (transaction, *send1).code);
	node1.active.start (send1);
	std::unique_lock<std::mutex> lock (node1.active.mutex);
	auto votes1 (node1.active.roots.find (send1->qualified_root ())->election);
	auto vote1 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 1, send1));
	auto channel (std::make_shared<btcb::transport::channel_udp> (node1.network.udp_channels, node1.network.endpoint ()));
	node1.vote_processor.vote_blocking (transaction, vote1, channel);
	btcb::keypair key2;
	auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto vote2 (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 2, send2));
	node1.vote_processor.vote_blocking (transaction, vote2, channel);
	ASSERT_EQ (2, votes1->last_votes.size ());
	ASSERT_NE (votes1->last_votes.end (), votes1->last_votes.find (btcb::test_genesis_key.pub));
	ASSERT_EQ (send1->hash (), votes1->last_votes[btcb::test_genesis_key.pub].hash);
	auto winner (*votes1->tally (transaction).begin ());
	ASSERT_EQ (*send1, *winner.second);
}

// Query for block successor
TEST (ledger, successor)
{
	btcb::system system (24000, 1);
	btcb::keypair key1;
	btcb::genesis genesis;
	btcb::send_block send1 (genesis.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
	system.nodes[0]->work_generate_blocking (send1);
	auto transaction (system.nodes[0]->store.tx_begin_write ());
	ASSERT_EQ (btcb::process_result::progress, system.nodes[0]->ledger.process (transaction, send1).code);
	ASSERT_EQ (send1, *system.nodes[0]->ledger.successor (transaction, btcb::qualified_root (genesis.hash (), 0)));
	ASSERT_EQ (*genesis.open, *system.nodes[0]->ledger.successor (transaction, genesis.open->qualified_root ()));
	ASSERT_EQ (nullptr, system.nodes[0]->ledger.successor (transaction, btcb::qualified_root (0)));
}

TEST (ledger, fail_change_old)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::change_block block (genesis.hash (), key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (btcb::process_result::progress, result1.code);
	auto result2 (ledger.process (transaction, block));
	ASSERT_EQ (btcb::process_result::old, result2.code);
}

TEST (ledger, fail_change_gap_previous)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::change_block block (1, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (1));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (btcb::process_result::gap_previous, result1.code);
}

TEST (ledger, fail_change_bad_signature)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::change_block block (genesis.hash (), key1.pub, btcb::keypair ().prv, 0, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (btcb::process_result::bad_signature, result1.code);
}

TEST (ledger, fail_change_fork)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::change_block block1 (genesis.hash (), key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (btcb::process_result::progress, result1.code);
	btcb::keypair key2;
	btcb::change_block block2 (genesis.hash (), key2.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (btcb::process_result::fork, result2.code);
}

TEST (ledger, fail_send_old)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (btcb::process_result::progress, result1.code);
	auto result2 (ledger.process (transaction, block));
	ASSERT_EQ (btcb::process_result::old, result2.code);
}

TEST (ledger, fail_send_gap_previous)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block (1, key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (1));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (btcb::process_result::gap_previous, result1.code);
}

TEST (ledger, fail_send_bad_signature)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block (genesis.hash (), key1.pub, 1, btcb::keypair ().prv, 0, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block));
	ASSERT_EQ (btcb::process_result::bad_signature, result1.code);
}

TEST (ledger, fail_send_negative_spend)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
	btcb::keypair key2;
	btcb::send_block block2 (block1.hash (), key2.pub, 2, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	ASSERT_EQ (btcb::process_result::negative_spend, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_send_fork)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
	btcb::keypair key2;
	btcb::send_block block2 (genesis.hash (), key2.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::fork, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_old)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
	btcb::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (btcb::process_result::old, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_gap_source)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::open_block block2 (1, 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (btcb::process_result::gap_source, result2.code);
}

TEST (ledger, fail_open_bad_signature)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
	btcb::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	block2.signature.clear ();
	ASSERT_EQ (btcb::process_result::bad_signature, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_open_fork_previous)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
	btcb::send_block block2 (block1.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block2).code);
	btcb::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block3).code);
	btcb::open_block block4 (block2.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (btcb::process_result::fork, ledger.process (transaction, block4).code);
}

TEST (ledger, fail_open_account_mismatch)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
	btcb::keypair badkey;
	btcb::open_block block2 (block1.hash (), 1, badkey.pub, badkey.prv, badkey.pub, pool.generate (badkey.pub));
	ASSERT_NE (btcb::process_result::progress, ledger.process (transaction, block2).code);
}

TEST (ledger, fail_receive_old)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
	btcb::send_block block2 (block1.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block2).code);
	btcb::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block3).code);
	btcb::receive_block block4 (block3.hash (), block2.hash (), key1.prv, key1.pub, pool.generate (block3.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (btcb::process_result::old, ledger.process (transaction, block4).code);
}

TEST (ledger, fail_receive_gap_source)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (btcb::process_result::progress, result1.code);
	btcb::send_block block2 (block1.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (btcb::process_result::progress, result2.code);
	btcb::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (btcb::process_result::progress, result3.code);
	btcb::receive_block block4 (block3.hash (), 1, key1.prv, key1.pub, pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (btcb::process_result::gap_source, result4.code);
}

TEST (ledger, fail_receive_overreceive)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (btcb::process_result::progress, result1.code);
	btcb::open_block block2 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block2));
	ASSERT_EQ (btcb::process_result::progress, result3.code);
	btcb::receive_block block3 (block2.hash (), block1.hash (), key1.prv, key1.pub, pool.generate (block2.hash ()));
	auto result4 (ledger.process (transaction, block3));
	ASSERT_EQ (btcb::process_result::unreceivable, result4.code);
}

TEST (ledger, fail_receive_bad_signature)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (btcb::process_result::progress, result1.code);
	btcb::send_block block2 (block1.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (btcb::process_result::progress, result2.code);
	btcb::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (btcb::process_result::progress, result3.code);
	btcb::receive_block block4 (block3.hash (), block2.hash (), btcb::keypair ().prv, 0, pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (btcb::process_result::bad_signature, result4.code);
}

TEST (ledger, fail_receive_gap_previous_opened)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (btcb::process_result::progress, result1.code);
	btcb::send_block block2 (block1.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (btcb::process_result::progress, result2.code);
	btcb::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (btcb::process_result::progress, result3.code);
	btcb::receive_block block4 (1, block2.hash (), key1.prv, key1.pub, pool.generate (1));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (btcb::process_result::gap_previous, result4.code);
}

TEST (ledger, fail_receive_gap_previous_unopened)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (btcb::process_result::progress, result1.code);
	btcb::send_block block2 (block1.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (btcb::process_result::progress, result2.code);
	btcb::receive_block block3 (1, block2.hash (), key1.prv, key1.pub, pool.generate (1));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (btcb::process_result::gap_previous, result3.code);
}

TEST (ledger, fail_receive_fork_previous)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (btcb::process_result::progress, result1.code);
	btcb::send_block block2 (block1.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (btcb::process_result::progress, result2.code);
	btcb::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (btcb::process_result::progress, result3.code);
	btcb::keypair key2;
	btcb::send_block block4 (block3.hash (), key1.pub, 1, key1.prv, key1.pub, pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (btcb::process_result::progress, result4.code);
	btcb::receive_block block5 (block3.hash (), block2.hash (), key1.prv, key1.pub, pool.generate (block3.hash ()));
	auto result5 (ledger.process (transaction, block5));
	ASSERT_EQ (btcb::process_result::fork, result5.code);
}

TEST (ledger, fail_receive_received_source)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key1;
	btcb::send_block block1 (genesis.hash (), key1.pub, 2, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	auto result1 (ledger.process (transaction, block1));
	ASSERT_EQ (btcb::process_result::progress, result1.code);
	btcb::send_block block2 (block1.hash (), key1.pub, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	auto result2 (ledger.process (transaction, block2));
	ASSERT_EQ (btcb::process_result::progress, result2.code);
	btcb::send_block block6 (block2.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block2.hash ()));
	auto result6 (ledger.process (transaction, block6));
	ASSERT_EQ (btcb::process_result::progress, result6.code);
	btcb::open_block block3 (block1.hash (), 1, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto result3 (ledger.process (transaction, block3));
	ASSERT_EQ (btcb::process_result::progress, result3.code);
	btcb::keypair key2;
	btcb::send_block block4 (block3.hash (), key1.pub, 1, key1.prv, key1.pub, pool.generate (block3.hash ()));
	auto result4 (ledger.process (transaction, block4));
	ASSERT_EQ (btcb::process_result::progress, result4.code);
	btcb::receive_block block5 (block4.hash (), block2.hash (), key1.prv, key1.pub, pool.generate (block4.hash ()));
	auto result5 (ledger.process (transaction, block5));
	ASSERT_EQ (btcb::process_result::progress, result5.code);
	btcb::receive_block block7 (block3.hash (), block2.hash (), key1.prv, key1.pub, pool.generate (block3.hash ()));
	auto result7 (ledger.process (transaction, block7));
	ASSERT_EQ (btcb::process_result::fork, result7.code);
}

TEST (ledger, latest_empty)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::keypair key;
	auto transaction (store.tx_begin_read ());
	auto latest (ledger.latest (transaction, key.pub));
	ASSERT_TRUE (latest.is_zero ());
}

TEST (ledger, latest_root)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_FALSE (init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key;
	ASSERT_EQ (key.pub, ledger.latest_root (transaction, key.pub));
	auto hash1 (ledger.latest (transaction, btcb::test_genesis_key.pub));
	btcb::send_block send (hash1, 0, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (hash1));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (send.hash (), ledger.latest_root (transaction, btcb::test_genesis_key.pub));
}

TEST (ledger, change_representative_move_representation)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::keypair key1;
	auto transaction (store.tx_begin_write ());
	btcb::genesis genesis;
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	auto hash1 (genesis.hash ());
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::test_genesis_key.pub));
	btcb::send_block send (hash1, key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (hash1));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send).code);
	ASSERT_EQ (0, ledger.weight (transaction, btcb::test_genesis_key.pub));
	btcb::keypair key2;
	btcb::change_block change (send.hash (), key2.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, change).code);
	btcb::keypair key3;
	btcb::open_block open (send.hash (), key3.pub, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open).code);
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, key3.pub));
}

TEST (ledger, send_open_receive_rollback)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	auto transaction (store.tx_begin_write ());
	btcb::genesis genesis;
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::account_info info1;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
	btcb::keypair key1;
	btcb::send_block send1 (info1.head, key1.pub, btcb::genesis_amount - 50, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
	auto return1 (ledger.process (transaction, send1));
	ASSERT_EQ (btcb::process_result::progress, return1.code);
	btcb::send_block send2 (send1.hash (), key1.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	auto return2 (ledger.process (transaction, send2));
	ASSERT_EQ (btcb::process_result::progress, return2.code);
	btcb::keypair key2;
	btcb::open_block open (send2.hash (), key2.pub, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	auto return4 (ledger.process (transaction, open));
	ASSERT_EQ (btcb::process_result::progress, return4.code);
	btcb::receive_block receive (open.hash (), send1.hash (), key1.prv, key1.pub, pool.generate (open.hash ()));
	auto return5 (ledger.process (transaction, receive));
	ASSERT_EQ (btcb::process_result::progress, return5.code);
	btcb::keypair key3;
	ASSERT_EQ (100, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (btcb::genesis_amount - 100, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	btcb::change_block change1 (send2.hash (), key3.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send2.hash ()));
	auto return6 (ledger.process (transaction, change1));
	ASSERT_EQ (btcb::process_result::progress, return6.code);
	ASSERT_EQ (100, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (btcb::genesis_amount - 100, ledger.weight (transaction, key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, receive.hash ()));
	ASSERT_EQ (50, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (btcb::genesis_amount - 100, ledger.weight (transaction, key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, open.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_EQ (btcb::genesis_amount - 100, ledger.weight (transaction, key3.pub));
	ASSERT_FALSE (ledger.rollback (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (btcb::genesis_amount - 100, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_FALSE (ledger.rollback (transaction, send2.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (btcb::genesis_amount - 50, ledger.weight (transaction, btcb::test_genesis_key.pub));
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key3.pub));
	ASSERT_EQ (btcb::genesis_amount - 0, ledger.weight (transaction, btcb::test_genesis_key.pub));
}

TEST (ledger, bootstrap_rep_weight)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::account_info info1;
	btcb::keypair key2;
	btcb::genesis genesis;
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	{
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis);
		ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
		btcb::send_block send (info1.head, key2.pub, std::numeric_limits<btcb::uint128_t>::max () - 50, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
		ledger.process (transaction, send);
	}
	{
		auto transaction (store.tx_begin_read ());
		ledger.bootstrap_weight_max_blocks = 3;
		ledger.bootstrap_weights[key2.pub] = 1000;
		ASSERT_EQ (1000, ledger.weight (transaction, key2.pub));
	}
	{
		auto transaction (store.tx_begin_write ());
		ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info1));
		btcb::send_block send (info1.head, key2.pub, std::numeric_limits<btcb::uint128_t>::max () - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info1.head));
		ledger.process (transaction, send);
	}
	{
		auto transaction (store.tx_begin_read ());
		ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	}
}

TEST (ledger, block_destination_source)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair dest;
	btcb::uint128_t balance (btcb::genesis_amount);
	balance -= btcb::Gbcb_ratio;
	btcb::send_block block1 (genesis.hash (), dest.pub, balance, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	balance -= btcb::Gbcb_ratio;
	btcb::send_block block2 (block1.hash (), btcb::genesis_account, balance, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block1.hash ()));
	balance += btcb::Gbcb_ratio;
	btcb::receive_block block3 (block2.hash (), block2.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block2.hash ()));
	balance -= btcb::Gbcb_ratio;
	btcb::state_block block4 (btcb::genesis_account, block3.hash (), btcb::genesis_account, balance, dest.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block3.hash ()));
	balance -= btcb::Gbcb_ratio;
	btcb::state_block block5 (btcb::genesis_account, block4.hash (), btcb::genesis_account, balance, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block4.hash ()));
	balance += btcb::Gbcb_ratio;
	btcb::state_block block6 (btcb::genesis_account, block5.hash (), btcb::genesis_account, balance, block5.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (block5.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block3).code);
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block4).code);
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block5).code);
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block6).code);
	ASSERT_EQ (balance, ledger.balance (transaction, block6.hash ()));
	ASSERT_EQ (dest.pub, ledger.block_destination (transaction, block1));
	ASSERT_TRUE (ledger.block_source (transaction, block1).is_zero ());
	ASSERT_EQ (btcb::genesis_account, ledger.block_destination (transaction, block2));
	ASSERT_TRUE (ledger.block_source (transaction, block2).is_zero ());
	ASSERT_TRUE (ledger.block_destination (transaction, block3).is_zero ());
	ASSERT_EQ (block2.hash (), ledger.block_source (transaction, block3));
	ASSERT_EQ (dest.pub, ledger.block_destination (transaction, block4));
	ASSERT_TRUE (ledger.block_source (transaction, block4).is_zero ());
	ASSERT_EQ (btcb::genesis_account, ledger.block_destination (transaction, block5));
	ASSERT_TRUE (ledger.block_source (transaction, block5).is_zero ());
	ASSERT_TRUE (ledger.block_destination (transaction, block6).is_zero ());
	ASSERT_EQ (block5.hash (), ledger.block_source (transaction, block6));
}

TEST (ledger, state_account)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_EQ (btcb::genesis_account, ledger.account (transaction, send1.hash ()));
}

TEST (ledger, state_send_receive)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_TRUE (store.pending_exists (transaction, btcb::pending_key (btcb::genesis_account, send1.hash ())));
	btcb::state_block receive1 (btcb::genesis_account, send1.hash (), btcb::genesis_account, btcb::genesis_amount, send1.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store.block_exists (transaction, receive1.hash ()));
	auto receive2 (store.block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (btcb::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_FALSE (store.pending_exists (transaction, btcb::pending_key (btcb::genesis_account, send1.hash ())));
}

TEST (ledger, state_receive)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::send_block send1 (genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	btcb::state_block receive1 (btcb::genesis_account, send1.hash (), btcb::genesis_account, btcb::genesis_amount, send1.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store.block_exists (transaction, receive1.hash ()));
	auto receive2 (store.block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (btcb::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::genesis_account));
}

TEST (ledger, state_rep_change)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair rep;
	btcb::state_block change1 (btcb::genesis_account, genesis.hash (), rep.pub, btcb::genesis_amount, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_TRUE (store.block_exists (transaction, change1.hash ()));
	auto change2 (store.block_get (transaction, change1.hash ()));
	ASSERT_NE (nullptr, change2);
	ASSERT_EQ (change1, *change2);
	ASSERT_EQ (btcb::genesis_amount, ledger.balance (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.amount (transaction, change1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_open)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_TRUE (store.pending_exists (transaction, btcb::pending_key (destination.pub, send1.hash ())));
	btcb::state_block open1 (destination.pub, 0, btcb::genesis_account, btcb::Gbcb_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (store.pending_exists (transaction, btcb::pending_key (destination.pub, send1.hash ())));
	ASSERT_TRUE (store.block_exists (transaction, open1.hash ()));
	auto open2 (store.block_get (transaction, open1.hash ()));
	ASSERT_NE (nullptr, open2);
	ASSERT_EQ (open1, *open2);
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.balance (transaction, open1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, open1.hash ()));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::genesis_account));
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, send_after_state_fail)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::send_block send2 (send1.hash (), btcb::genesis_account, btcb::genesis_amount - (2 * btcb::Gbcb_ratio), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::block_position, ledger.process (transaction, send2).code);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, receive_after_state_fail)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::receive_block receive1 (send1.hash (), send1.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::block_position, ledger.process (transaction, receive1).code);
}

// Make sure old block types can't be inserted after a state block.
TEST (ledger, change_after_state_fail)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::keypair rep;
	btcb::change_block change1 (send1.hash (), rep.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::block_position, ledger.process (transaction, change1).code);
}

TEST (ledger, state_unreceivable_fail)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::send_block send1 (genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	btcb::state_block receive1 (btcb::genesis_account, send1.hash (), btcb::genesis_account, btcb::genesis_amount, 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::gap_source, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_receive_bad_amount_fail)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::send_block send1 (genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	btcb::state_block receive1 (btcb::genesis_account, send1.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, send1.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::balance_mismatch, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_no_link_amount_fail)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::keypair rep;
	btcb::state_block change1 (btcb::genesis_account, send1.hash (), rep.pub, btcb::genesis_amount, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::balance_mismatch, ledger.process (transaction, change1).code);
}

TEST (ledger, state_receive_wrong_account_fail)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	btcb::keypair key;
	btcb::state_block receive1 (key.pub, 0, btcb::genesis_account, btcb::Gbcb_ratio, send1.hash (), key.prv, key.pub, pool.generate (key.pub));
	ASSERT_EQ (btcb::process_result::unreceivable, ledger.process (transaction, receive1).code);
}

TEST (ledger, state_open_state_fork)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::state_block open1 (destination.pub, 0, btcb::genesis_account, btcb::Gbcb_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	btcb::open_block open2 (send1.hash (), btcb::genesis_account, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::fork, ledger.process (transaction, open2).code);
	ASSERT_EQ (open1.root (), open2.root ());
}

TEST (ledger, state_state_open_fork)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::open_block open1 (send1.hash (), btcb::genesis_account, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	btcb::state_block open2 (destination.pub, 0, btcb::genesis_account, btcb::Gbcb_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::fork, ledger.process (transaction, open2).code);
	ASSERT_EQ (open1.root (), open2.root ());
}

TEST (ledger, state_open_previous_fail)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::state_block open1 (destination.pub, destination.pub, btcb::genesis_account, btcb::Gbcb_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::gap_previous, ledger.process (transaction, open1).code);
}

TEST (ledger, state_open_source_fail)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::state_block open1 (destination.pub, 0, btcb::genesis_account, 0, 0, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::gap_source, ledger.process (transaction, open1).code);
}

TEST (ledger, state_send_change)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair rep;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), rep.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_receive_change)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.balance (transaction, send1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, send1.hash ()));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	btcb::keypair rep;
	btcb::state_block receive1 (btcb::genesis_account, send1.hash (), rep.pub, btcb::genesis_amount, send1.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (store.block_exists (transaction, receive1.hash ()));
	auto receive2 (store.block_get (transaction, receive1.hash ()));
	ASSERT_NE (nullptr, receive2);
	ASSERT_EQ (receive1, *receive2);
	ASSERT_EQ (btcb::genesis_amount, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (0, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_open_old)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::open_block open1 (send1.hash (), btcb::genesis_account, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.balance (transaction, open1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, open1.hash ()));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::genesis_account));
}

TEST (ledger, state_receive_old)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::state_block send2 (btcb::genesis_account, send1.hash (), btcb::genesis_account, btcb::genesis_amount - (2 * btcb::Gbcb_ratio), destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send2).code);
	btcb::open_block open1 (send1.hash (), btcb::genesis_account, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	btcb::receive_block receive1 (open1.hash (), send2.hash (), destination.prv, destination.pub, pool.generate (open1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_EQ (2 * btcb::Gbcb_ratio, ledger.balance (transaction, receive1.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, receive1.hash ()));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::genesis_account));
}

TEST (ledger, state_rollback_send)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_TRUE (store.block_exists (transaction, send1.hash ()));
	auto send2 (store.block_get (transaction, send1.hash ()));
	ASSERT_NE (nullptr, send2);
	ASSERT_EQ (send1, *send2);
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.account_balance (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	btcb::pending_info info;
	ASSERT_FALSE (store.pending_get (transaction, btcb::pending_key (btcb::genesis_account, send1.hash ()), info));
	ASSERT_EQ (btcb::genesis_account, info.source);
	ASSERT_EQ (btcb::Gbcb_ratio, info.amount.number ());
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store.block_exists (transaction, send1.hash ()));
	ASSERT_EQ (btcb::genesis_amount, ledger.account_balance (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_FALSE (store.pending_exists (transaction, btcb::pending_key (btcb::genesis_account, send1.hash ())));
	ASSERT_TRUE (store.block_successor (transaction, genesis.hash ()).is_zero ());
}

TEST (ledger, state_rollback_receive)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::state_block receive1 (btcb::genesis_account, send1.hash (), btcb::genesis_account, btcb::genesis_amount, send1.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store.pending_exists (transaction, btcb::pending_key (btcb::genesis_account, receive1.hash ())));
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	btcb::pending_info info;
	ASSERT_FALSE (store.pending_get (transaction, btcb::pending_key (btcb::genesis_account, send1.hash ()), info));
	ASSERT_EQ (btcb::genesis_account, info.source);
	ASSERT_EQ (btcb::Gbcb_ratio, info.amount.number ());
	ASSERT_FALSE (store.block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.account_balance (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
}

TEST (ledger, state_rollback_received_send)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair key;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::state_block receive1 (key.pub, 0, key.pub, btcb::Gbcb_ratio, send1.hash (), key.prv, key.pub, pool.generate (key.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (store.pending_exists (transaction, btcb::pending_key (btcb::genesis_account, receive1.hash ())));
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store.pending_exists (transaction, btcb::pending_key (btcb::genesis_account, send1.hash ())));
	ASSERT_FALSE (store.block_exists (transaction, send1.hash ()));
	ASSERT_FALSE (store.block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (btcb::genesis_amount, ledger.account_balance (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_EQ (0, ledger.account_balance (transaction, key.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key.pub));
}

TEST (ledger, state_rep_change_rollback)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair rep;
	btcb::state_block change1 (btcb::genesis_account, genesis.hash (), rep.pub, btcb::genesis_amount, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_FALSE (ledger.rollback (transaction, change1.hash ()));
	ASSERT_FALSE (store.block_exists (transaction, change1.hash ()));
	ASSERT_EQ (btcb::genesis_amount, ledger.account_balance (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_EQ (0, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_open_rollback)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::state_block open1 (destination.pub, 0, btcb::genesis_account, btcb::Gbcb_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_FALSE (ledger.rollback (transaction, open1.hash ()));
	ASSERT_FALSE (store.block_exists (transaction, open1.hash ()));
	ASSERT_EQ (0, ledger.account_balance (transaction, destination.pub));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	btcb::pending_info info;
	ASSERT_FALSE (store.pending_get (transaction, btcb::pending_key (destination.pub, send1.hash ()), info));
	ASSERT_EQ (btcb::genesis_account, info.source);
	ASSERT_EQ (btcb::Gbcb_ratio, info.amount.number ());
}

TEST (ledger, state_send_change_rollback)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair rep;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), rep.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_FALSE (ledger.rollback (transaction, send1.hash ()));
	ASSERT_FALSE (store.block_exists (transaction, send1.hash ()));
	ASSERT_EQ (btcb::genesis_amount, ledger.account_balance (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_EQ (0, ledger.weight (transaction, rep.pub));
}

TEST (ledger, state_receive_change_rollback)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::keypair rep;
	btcb::state_block receive1 (btcb::genesis_account, send1.hash (), rep.pub, btcb::genesis_amount, send1.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_FALSE (ledger.rollback (transaction, receive1.hash ()));
	ASSERT_FALSE (store.block_exists (transaction, receive1.hash ()));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.account_balance (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_EQ (0, ledger.weight (transaction, rep.pub));
}

TEST (ledger, epoch_blocks_general)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::keypair epoch_key;
	btcb::ledger ledger (store, stats, 123, epoch_key.pub);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::state_block epoch1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount, 123, epoch_key.prv, epoch_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, epoch1).code);
	btcb::state_block epoch2 (btcb::genesis_account, epoch1.hash (), btcb::genesis_account, btcb::genesis_amount, 123, epoch_key.prv, epoch_key.pub, pool.generate (epoch1.hash ()));
	ASSERT_EQ (btcb::process_result::block_position, ledger.process (transaction, epoch2).code);
	btcb::account_info genesis_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, btcb::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch, btcb::epoch::epoch_1);
	ASSERT_FALSE (ledger.rollback (transaction, epoch1.hash ()));
	ASSERT_FALSE (ledger.store.account_get (transaction, btcb::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch, btcb::epoch::epoch_0);
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, btcb::genesis_account, genesis_info));
	ASSERT_EQ (genesis_info.epoch, btcb::epoch::epoch_1);
	btcb::change_block change1 (epoch1.hash (), btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (epoch1.hash ()));
	ASSERT_EQ (btcb::process_result::block_position, ledger.process (transaction, change1).code);
	btcb::state_block send1 (btcb::genesis_account, epoch1.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (epoch1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::open_block open1 (send1.hash (), btcb::genesis_account, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::unreceivable, ledger.process (transaction, open1).code);
	btcb::state_block epoch3 (destination.pub, 0, btcb::genesis_account, 0, 123, epoch_key.prv, epoch_key.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::representative_mismatch, ledger.process (transaction, epoch3).code);
	btcb::state_block epoch4 (destination.pub, 0, 0, 0, 123, epoch_key.prv, epoch_key.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, epoch4).code);
	btcb::receive_block receive1 (epoch4.hash (), send1.hash (), destination.prv, destination.pub, pool.generate (epoch4.hash ()));
	ASSERT_EQ (btcb::process_result::block_position, ledger.process (transaction, receive1).code);
	btcb::state_block receive2 (destination.pub, epoch4.hash (), destination.pub, btcb::Gbcb_ratio, send1.hash (), destination.prv, destination.pub, pool.generate (epoch4.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_EQ (0, ledger.balance (transaction, epoch4.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.balance (transaction, receive2.hash ()));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.amount (transaction, receive2.hash ()));
	ASSERT_EQ (btcb::genesis_amount - btcb::Gbcb_ratio, ledger.weight (transaction, btcb::genesis_account));
	ASSERT_EQ (btcb::Gbcb_ratio, ledger.weight (transaction, destination.pub));
}

TEST (ledger, epoch_blocks_receive_upgrade)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::keypair epoch_key;
	btcb::ledger ledger (store, stats, 123, epoch_key.pub);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::state_block send1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::state_block epoch1 (btcb::genesis_account, send1.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, 123, epoch_key.prv, epoch_key.pub, pool.generate (send1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, epoch1).code);
	btcb::state_block send2 (btcb::genesis_account, epoch1.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio * 2, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (epoch1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send2).code);
	btcb::open_block open1 (send1.hash (), destination.pub, destination.pub, destination.prv, destination.pub, pool.generate (destination.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	btcb::receive_block receive1 (open1.hash (), send2.hash (), destination.prv, destination.pub, pool.generate (open1.hash ()));
	ASSERT_EQ (btcb::process_result::unreceivable, ledger.process (transaction, receive1).code);
	btcb::state_block receive2 (destination.pub, open1.hash (), destination.pub, btcb::Gbcb_ratio * 2, send2.hash (), destination.prv, destination.pub, pool.generate (open1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive2).code);
	btcb::account_info destination_info;
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch, btcb::epoch::epoch_1);
	ASSERT_FALSE (ledger.rollback (transaction, receive2.hash ()));
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch, btcb::epoch::epoch_0);
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive2).code);
	ASSERT_FALSE (ledger.store.account_get (transaction, destination.pub, destination_info));
	ASSERT_EQ (destination_info.epoch, btcb::epoch::epoch_1);
	btcb::keypair destination2;
	btcb::state_block send3 (destination.pub, receive2.hash (), destination.pub, btcb::Gbcb_ratio, destination2.pub, destination.prv, destination.pub, pool.generate (receive2.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send3).code);
	btcb::open_block open2 (send3.hash (), destination2.pub, destination2.pub, destination2.prv, destination2.pub, pool.generate (destination2.pub));
	ASSERT_EQ (btcb::process_result::unreceivable, ledger.process (transaction, open2).code);
}

TEST (ledger, epoch_blocks_fork)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::keypair epoch_key;
	btcb::ledger ledger (store, stats, 123, epoch_key.pub);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	btcb::send_block send1 (genesis.hash (), btcb::account (0), btcb::genesis_amount, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	btcb::state_block epoch1 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount, 123, epoch_key.prv, epoch_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::fork, ledger.process (transaction, epoch1).code);
}

TEST (ledger, could_fit)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	btcb::keypair epoch_key;
	btcb::ledger ledger (store, stats, 123, epoch_key.pub);
	btcb::keypair epoch_signer;
	ledger.epoch_link = 123;
	ledger.epoch_signer = epoch_signer.pub;
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::keypair destination;
	// Test legacy and state change blocks could_fit
	btcb::change_block change1 (genesis.hash (), btcb::genesis_account, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	btcb::state_block change2 (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_TRUE (ledger.could_fit (transaction, change1));
	ASSERT_TRUE (ledger.could_fit (transaction, change2));
	// Test legacy and state send
	btcb::keypair key1;
	btcb::send_block send1 (change1.hash (), key1.pub, btcb::genesis_amount - 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (change1.hash ()));
	btcb::state_block send2 (btcb::genesis_account, change1.hash (), btcb::genesis_account, btcb::genesis_amount - 1, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (change1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, send1));
	ASSERT_FALSE (ledger.could_fit (transaction, send2));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, change1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, change1));
	ASSERT_TRUE (ledger.could_fit (transaction, change2));
	ASSERT_TRUE (ledger.could_fit (transaction, send1));
	ASSERT_TRUE (ledger.could_fit (transaction, send2));
	// Test legacy and state open
	btcb::open_block open1 (send2.hash (), btcb::genesis_account, key1.pub, key1.prv, key1.pub, pool.generate (key1.pub));
	btcb::state_block open2 (key1.pub, 0, btcb::genesis_account, 1, send2.hash (), key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_FALSE (ledger.could_fit (transaction, open1));
	ASSERT_FALSE (ledger.could_fit (transaction, open2));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send2).code);
	ASSERT_TRUE (ledger.could_fit (transaction, send1));
	ASSERT_TRUE (ledger.could_fit (transaction, send2));
	ASSERT_TRUE (ledger.could_fit (transaction, open1));
	ASSERT_TRUE (ledger.could_fit (transaction, open2));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, open1));
	ASSERT_TRUE (ledger.could_fit (transaction, open2));
	// Create another send to receive
	btcb::state_block send3 (btcb::genesis_account, send2.hash (), btcb::genesis_account, btcb::genesis_amount - 2, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send2.hash ()));
	// Test legacy and state receive
	btcb::receive_block receive1 (open1.hash (), send3.hash (), key1.prv, key1.pub, pool.generate (open1.hash ()));
	btcb::state_block receive2 (key1.pub, open1.hash (), btcb::genesis_account, 2, send3.hash (), key1.prv, key1.pub, pool.generate (open1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, receive1));
	ASSERT_FALSE (ledger.could_fit (transaction, receive2));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send3).code);
	ASSERT_TRUE (ledger.could_fit (transaction, receive1));
	ASSERT_TRUE (ledger.could_fit (transaction, receive2));
	// Test epoch (state)
	btcb::state_block epoch1 (key1.pub, receive1.hash (), btcb::genesis_account, 2, ledger.epoch_link, epoch_signer.prv, epoch_signer.pub, pool.generate (receive1.hash ()));
	ASSERT_FALSE (ledger.could_fit (transaction, epoch1));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, receive1));
	ASSERT_TRUE (ledger.could_fit (transaction, receive2));
	ASSERT_TRUE (ledger.could_fit (transaction, epoch1));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, epoch1).code);
	ASSERT_TRUE (ledger.could_fit (transaction, epoch1));
}

TEST (ledger, unchecked_epoch)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair destination;
	auto send1 (std::make_shared<btcb::state_block> (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<btcb::state_block> (destination.pub, 0, destination.pub, btcb::Gbcb_ratio, send1->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	auto epoch1 (std::make_shared<btcb::state_block> (destination.pub, open1->hash (), destination.pub, btcb::Gbcb_ratio, node1.ledger.epoch_link, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*epoch1);
	node1.block_processor.add (epoch1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		auto blocks (node1.store.unchecked_get (transaction, epoch1->previous ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, btcb::signature_verification::valid_epoch);
	}
	node1.block_processor.add (send1);
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, epoch1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		btcb::account_info info;
		ASSERT_FALSE (node1.store.account_get (transaction, destination.pub, info));
		ASSERT_EQ (info.epoch, btcb::epoch::epoch_1);
	}
}

TEST (ledger, unchecked_epoch_invalid)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair destination;
	auto send1 (std::make_shared<btcb::state_block> (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<btcb::state_block> (destination.pub, 0, destination.pub, btcb::Gbcb_ratio, send1->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	// Epoch block with account own signature
	auto epoch1 (std::make_shared<btcb::state_block> (destination.pub, open1->hash (), destination.pub, btcb::Gbcb_ratio, node1.ledger.epoch_link, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*epoch1);
	// Pseudo epoch block (send subtype, destination - epoch link)
	auto epoch2 (std::make_shared<btcb::state_block> (destination.pub, open1->hash (), destination.pub, btcb::Gbcb_ratio - 1, node1.ledger.epoch_link, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*epoch2);
	node1.block_processor.add (epoch1);
	node1.block_processor.add (epoch2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 2);
		auto blocks (node1.store.unchecked_get (transaction, epoch1->previous ()));
		ASSERT_EQ (blocks.size (), 2);
		ASSERT_EQ (blocks[0].verified, btcb::signature_verification::valid);
		ASSERT_EQ (blocks[1].verified, btcb::signature_verification::valid);
	}
	node1.block_processor.add (send1);
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_FALSE (node1.store.block_exists (transaction, epoch1->hash ()));
		ASSERT_TRUE (node1.store.block_exists (transaction, epoch2->hash ()));
		ASSERT_TRUE (node1.active.empty ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
		btcb::account_info info;
		ASSERT_FALSE (node1.store.account_get (transaction, destination.pub, info));
		ASSERT_NE (info.epoch, btcb::epoch::epoch_1);
	}
}

TEST (ledger, unchecked_open)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair destination;
	auto send1 (std::make_shared<btcb::state_block> (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto open1 (std::make_shared<btcb::open_block> (send1->hash (), destination.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	// Invalid signature for open block
	auto open2 (std::make_shared<btcb::open_block> (send1->hash (), btcb::test_genesis_key.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open2);
	open2->signature.bytes[0] ^= 1;
	node1.block_processor.add (open1);
	node1.block_processor.add (open2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		auto blocks (node1.store.unchecked_get (transaction, open1->source ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, btcb::signature_verification::valid);
	}
	node1.block_processor.add (send1);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, open1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
	}
}

TEST (ledger, unchecked_receive)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::genesis genesis;
	btcb::keypair destination;
	auto send1 (std::make_shared<btcb::state_block> (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<btcb::state_block> (btcb::genesis_account, send1->hash (), btcb::genesis_account, btcb::genesis_amount - 2 * btcb::Gbcb_ratio, destination.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	auto open1 (std::make_shared<btcb::open_block> (send1->hash (), destination.pub, destination.pub, destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*open1);
	auto receive1 (std::make_shared<btcb::receive_block> (open1->hash (), send2->hash (), destination.prv, destination.pub, 0));
	node1.work_generate_blocking (*receive1);
	node1.block_processor.add (send1);
	node1.block_processor.add (receive1);
	node1.block_processor.flush ();
	// Previous block for receive1 is unknown, signature cannot be validated
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		auto blocks (node1.store.unchecked_get (transaction, receive1->previous ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, btcb::signature_verification::unknown);
	}
	node1.block_processor.add (open1);
	node1.block_processor.flush ();
	// Previous block for receive1 is known, signature was validated
	{
		auto transaction (node1.store.tx_begin_read ());
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
		auto blocks (node1.store.unchecked_get (transaction, receive1->source ()));
		ASSERT_EQ (blocks.size (), 1);
		ASSERT_EQ (blocks[0].verified, btcb::signature_verification::valid);
	}
	node1.block_processor.add (send2);
	node1.block_processor.flush ();
	{
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, receive1->hash ()));
		auto unchecked_count (node1.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
	}
}

TEST (ledger, confirmation_height_not_updated)
{
	bool error (false);
	btcb::logger_mt logger;
	btcb::mdb_store store (error, logger, btcb::unique_path ());
	ASSERT_TRUE (!error);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	auto transaction (store.tx_begin_write ());
	btcb::genesis genesis;
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::account_info account_info;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, account_info));
	btcb::keypair key;
	btcb::send_block send1 (account_info.head, key.pub, 50, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (account_info.head));
	ASSERT_EQ (1, account_info.confirmation_height);
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send1).code);
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, account_info));
	ASSERT_EQ (1, account_info.confirmation_height);
	btcb::open_block open1 (send1.hash (), btcb::genesis_account, key.pub, key.prv, key.pub, pool.generate (key.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open1).code);
	btcb::account_info account_info1;
	ASSERT_FALSE (store.account_get (transaction, key.pub, account_info1));
	ASSERT_EQ (0, account_info1.confirmation_height);
}
