#include <gtest/gtest.h>
#include <btcb/core_test/testutil.hpp>
#include <btcb/crypto_lib/random_pool.hpp>
#include <btcb/lib/utility.hpp>
#include <btcb/node/common.hpp>
#include <btcb/node/node.hpp>
#include <btcb/secure/versioning.hpp>

#include <fstream>

namespace
{
void modify_account_info_to_v13 (btcb::mdb_store & store, btcb::transaction const & transaction_a, btcb::account const & account_a);
void modify_genesis_account_info_to_v5 (btcb::mdb_store & store, btcb::transaction const & transaction_a);
}

TEST (block_store, construction)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto now (btcb::seconds_since_epoch ());
	ASSERT_GT (now, 1408074640);
}

TEST (block_store, sideband_serialization)
{
	btcb::block_sideband sideband1;
	sideband1.type = btcb::block_type::receive;
	sideband1.account = 1;
	sideband1.balance = 2;
	sideband1.height = 3;
	sideband1.successor = 4;
	sideband1.timestamp = 5;
	std::vector<uint8_t> vector;
	{
		btcb::vectorstream stream1 (vector);
		sideband1.serialize (stream1);
	}
	btcb::bufferstream stream2 (vector.data (), vector.size ());
	btcb::block_sideband sideband2;
	sideband2.type = btcb::block_type::receive;
	ASSERT_FALSE (sideband2.deserialize (stream2));
	ASSERT_EQ (sideband1.account, sideband2.account);
	ASSERT_EQ (sideband1.balance, sideband2.balance);
	ASSERT_EQ (sideband1.height, sideband2.height);
	ASSERT_EQ (sideband1.successor, sideband2.successor);
	ASSERT_EQ (sideband1.timestamp, sideband2.timestamp);
}

TEST (block_store, add_item)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::open_block block (0, 1, 0, btcb::keypair ().prv, 0, 0);
	btcb::uint256_union hash1 (block.hash ());
	auto transaction (store.tx_begin_write ());
	auto latest1 (store.block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	ASSERT_FALSE (store.block_exists (transaction, hash1));
	btcb::block_sideband sideband (btcb::block_type::open, 0, 0, 0, 0, 0);
	store.block_put (transaction, hash1, block, sideband);
	auto latest2 (store.block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
	ASSERT_TRUE (store.block_exists (transaction, hash1));
	ASSERT_FALSE (store.block_exists (transaction, hash1.number () - 1));
	store.block_del (transaction, hash1);
	auto latest3 (store.block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest3);
}

TEST (block_store, clear_successor)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::open_block block1 (0, 1, 0, btcb::keypair ().prv, 0, 0);
	auto transaction (store.tx_begin_write ());
	btcb::block_sideband sideband (btcb::block_type::open, 0, 0, 0, 0, 0);
	store.block_put (transaction, block1.hash (), block1, sideband);
	btcb::open_block block2 (0, 2, 0, btcb::keypair ().prv, 0, 0);
	store.block_put (transaction, block2.hash (), block2, sideband);
	ASSERT_NE (nullptr, store.block_get (transaction, block1.hash (), &sideband));
	ASSERT_EQ (0, sideband.successor.number ());
	sideband.successor = block2.hash ();
	store.block_put (transaction, block1.hash (), block1, sideband);
	ASSERT_NE (nullptr, store.block_get (transaction, block1.hash (), &sideband));
	ASSERT_EQ (block2.hash (), sideband.successor);
	store.block_successor_clear (transaction, block1.hash ());
	ASSERT_NE (nullptr, store.block_get (transaction, block1.hash (), &sideband));
	ASSERT_EQ (0, sideband.successor.number ());
}

TEST (block_store, add_nonempty_block)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::keypair key1;
	btcb::open_block block (0, 1, 0, btcb::keypair ().prv, 0, 0);
	btcb::uint256_union hash1 (block.hash ());
	block.signature = btcb::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store.tx_begin_write ());
	auto latest1 (store.block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	btcb::block_sideband sideband (btcb::block_type::open, 0, 0, 0, 0, 0);
	store.block_put (transaction, hash1, block, sideband);
	auto latest2 (store.block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_two_items)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::keypair key1;
	btcb::open_block block (0, 1, 1, btcb::keypair ().prv, 0, 0);
	btcb::uint256_union hash1 (block.hash ());
	block.signature = btcb::sign_message (key1.prv, key1.pub, hash1);
	auto transaction (store.tx_begin_write ());
	auto latest1 (store.block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	btcb::open_block block2 (0, 1, 3, btcb::keypair ().prv, 0, 0);
	block2.hashables.account = 3;
	btcb::uint256_union hash2 (block2.hash ());
	block2.signature = btcb::sign_message (key1.prv, key1.pub, hash2);
	auto latest2 (store.block_get (transaction, hash2));
	ASSERT_EQ (nullptr, latest2);
	btcb::block_sideband sideband (btcb::block_type::open, 0, 0, 0, 0, 0);
	store.block_put (transaction, hash1, block, sideband);
	btcb::block_sideband sideband2 (btcb::block_type::open, 0, 0, 0, 0, 0);
	store.block_put (transaction, hash2, block2, sideband2);
	auto latest3 (store.block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest3);
	ASSERT_EQ (block, *latest3);
	auto latest4 (store.block_get (transaction, hash2));
	ASSERT_NE (nullptr, latest4);
	ASSERT_EQ (block2, *latest4);
	ASSERT_FALSE (*latest3 == *latest4);
}

TEST (block_store, add_receive)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::keypair key1;
	btcb::keypair key2;
	btcb::open_block block1 (0, 1, 0, btcb::keypair ().prv, 0, 0);
	auto transaction (store.tx_begin_write ());
	btcb::block_sideband sideband1 (btcb::block_type::open, 0, 0, 0, 0, 0);
	store.block_put (transaction, block1.hash (), block1, sideband1);
	btcb::receive_block block (block1.hash (), 1, btcb::keypair ().prv, 2, 3);
	btcb::block_hash hash1 (block.hash ());
	auto latest1 (store.block_get (transaction, hash1));
	ASSERT_EQ (nullptr, latest1);
	btcb::block_sideband sideband (btcb::block_type::receive, 0, 0, 0, 0, 0);
	store.block_put (transaction, hash1, block, sideband);
	auto latest2 (store.block_get (transaction, hash1));
	ASSERT_NE (nullptr, latest2);
	ASSERT_EQ (block, *latest2);
}

TEST (block_store, add_pending)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::keypair key1;
	btcb::pending_key key2 (0, 0);
	btcb::pending_info pending1;
	auto transaction (store.tx_begin_write ());
	ASSERT_TRUE (store.pending_get (transaction, key2, pending1));
	store.pending_put (transaction, key2, pending1);
	btcb::pending_info pending2;
	ASSERT_FALSE (store.pending_get (transaction, key2, pending2));
	ASSERT_EQ (pending1, pending2);
	store.pending_del (transaction, key2);
	ASSERT_TRUE (store.pending_get (transaction, key2, pending2));
}

TEST (block_store, pending_iterator)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin_write ());
	ASSERT_EQ (store.pending_end (), store.pending_begin (transaction));
	store.pending_put (transaction, btcb::pending_key (1, 2), { 2, 3, btcb::epoch::epoch_1 });
	auto current (store.pending_begin (transaction));
	ASSERT_NE (store.pending_end (), current);
	btcb::pending_key key1 (current->first);
	ASSERT_EQ (btcb::account (1), key1.account);
	ASSERT_EQ (btcb::block_hash (2), key1.hash);
	btcb::pending_info pending (current->second);
	ASSERT_EQ (btcb::account (2), pending.source);
	ASSERT_EQ (btcb::amount (3), pending.amount);
	ASSERT_EQ (btcb::epoch::epoch_1, pending.epoch);
}

/**
 * Regression test for Issue 1164
 * This reconstructs the situation where a key is larger in pending than the account being iterated in pending_v1, leaving
 * iteration order up to the value, causing undefined behavior.
 * After the bugfix, the value is compared only if the keys are equal.
 */
TEST (block_store, pending_iterator_comparison)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::stat stats;
	auto transaction (store.tx_begin_write ());
	// Populate pending
	store.pending_put (transaction, btcb::pending_key (btcb::account (3), btcb::block_hash (1)), btcb::pending_info (btcb::account (10), btcb::amount (1), btcb::epoch::epoch_0));
	store.pending_put (transaction, btcb::pending_key (btcb::account (3), btcb::block_hash (4)), btcb::pending_info (btcb::account (10), btcb::amount (0), btcb::epoch::epoch_0));
	// Populate pending_v1
	store.pending_put (transaction, btcb::pending_key (btcb::account (2), btcb::block_hash (2)), btcb::pending_info (btcb::account (10), btcb::amount (2), btcb::epoch::epoch_1));
	store.pending_put (transaction, btcb::pending_key (btcb::account (2), btcb::block_hash (3)), btcb::pending_info (btcb::account (10), btcb::amount (3), btcb::epoch::epoch_1));

	// Iterate account 3 (pending)
	{
		size_t count = 0;
		btcb::account begin (3);
		btcb::account end (begin.number () + 1);
		for (auto i (store.pending_begin (transaction, btcb::pending_key (begin, 0))), n (store.pending_begin (transaction, btcb::pending_key (end, 0))); i != n; ++i, ++count)
		{
			btcb::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}

	// Iterate account 2 (pending_v1)
	{
		size_t count = 0;
		btcb::account begin (2);
		btcb::account end (begin.number () + 1);
		for (auto i (store.pending_begin (transaction, btcb::pending_key (begin, 0))), n (store.pending_begin (transaction, btcb::pending_key (end, 0))); i != n; ++i, ++count)
		{
			btcb::pending_key key (i->first);
			ASSERT_EQ (key.account, begin);
			ASSERT_LT (count, 3);
		}
		ASSERT_EQ (count, 2);
	}
}

TEST (block_store, genesis)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::genesis genesis;
	auto hash (genesis.hash ());
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::account_info info;
	ASSERT_FALSE (store.account_get (transaction, btcb::genesis_account, info));
	ASSERT_EQ (hash, info.head);
	auto block1 (store.block_get (transaction, info.head));
	ASSERT_NE (nullptr, block1);
	auto receive1 (dynamic_cast<btcb::open_block *> (block1.get ()));
	ASSERT_NE (nullptr, receive1);
	ASSERT_LE (info.modified, btcb::seconds_since_epoch ());
	ASSERT_EQ (info.block_count, 1);
	// Genesis block should be confirmed by default
	ASSERT_EQ (info.confirmation_height, 1);
	auto test_pub_text (btcb::test_genesis_key.pub.to_string ());
	auto test_pub_account (btcb::test_genesis_key.pub.to_account ());
	auto test_prv_text (btcb::test_genesis_key.prv.data.to_string ());
	ASSERT_EQ (btcb::genesis_account, btcb::test_genesis_key.pub);
}

TEST (representation, changes)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::keypair key1;
	auto transaction (store.tx_begin_write ());
	ASSERT_EQ (0, store.representation_get (transaction, key1.pub));
	store.representation_put (transaction, key1.pub, 1);
	ASSERT_EQ (1, store.representation_get (transaction, key1.pub));
	store.representation_put (transaction, key1.pub, 2);
	ASSERT_EQ (2, store.representation_get (transaction, key1.pub));
}

TEST (bootstrap, simple)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto block1 (std::make_shared<btcb::send_block> (0, 1, 2, btcb::keypair ().prv, 4, 5));
	auto transaction (store.tx_begin_write ());
	auto block2 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store.unchecked_put (transaction, block1->previous (), block1);
	auto block3 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_FALSE (block3.empty ());
	ASSERT_EQ (*block1, *(block3[0].block));
	store.unchecked_del (transaction, btcb::unchecked_key (block1->previous (), block1->hash ()));
	auto block4 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block4.empty ());
}

TEST (unchecked, multiple)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto block1 (std::make_shared<btcb::send_block> (4, 1, 2, btcb::keypair ().prv, 4, 5));
	auto transaction (store.tx_begin_write ());
	auto block2 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store.unchecked_put (transaction, block1->previous (), block1);
	store.unchecked_put (transaction, block1->source (), block1);
	auto block3 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_FALSE (block3.empty ());
	auto block4 (store.unchecked_get (transaction, block1->source ()));
	ASSERT_FALSE (block4.empty ());
}

TEST (unchecked, double_put)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto block1 (std::make_shared<btcb::send_block> (4, 1, 2, btcb::keypair ().prv, 4, 5));
	auto transaction (store.tx_begin_write ());
	auto block2 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_TRUE (block2.empty ());
	store.unchecked_put (transaction, block1->previous (), block1);
	store.unchecked_put (transaction, block1->previous (), block1);
	auto block3 (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_EQ (block3.size (), 1);
}

TEST (unchecked, multiple_get)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto block1 (std::make_shared<btcb::send_block> (4, 1, 2, btcb::keypair ().prv, 4, 5));
	auto block2 (std::make_shared<btcb::send_block> (3, 1, 2, btcb::keypair ().prv, 4, 5));
	auto block3 (std::make_shared<btcb::send_block> (5, 1, 2, btcb::keypair ().prv, 4, 5));
	{
		auto transaction (store.tx_begin_write ());
		store.unchecked_put (transaction, block1->previous (), block1); // unchecked1
		store.unchecked_put (transaction, block1->hash (), block1); // unchecked2
		store.unchecked_put (transaction, block2->previous (), block2); // unchecked3
		store.unchecked_put (transaction, block1->previous (), block2); // unchecked1
		store.unchecked_put (transaction, block1->hash (), block2); // unchecked2
		store.unchecked_put (transaction, block3->previous (), block3);
		store.unchecked_put (transaction, block3->hash (), block3); // unchecked4
		store.unchecked_put (transaction, block1->previous (), block3); // unchecked1
	}
	auto transaction (store.tx_begin_read ());
	auto unchecked_count (store.unchecked_count (transaction));
	ASSERT_EQ (unchecked_count, 8);
	std::vector<btcb::block_hash> unchecked1;
	auto unchecked1_blocks (store.unchecked_get (transaction, block1->previous ()));
	ASSERT_EQ (unchecked1_blocks.size (), 3);
	for (auto & i : unchecked1_blocks)
	{
		unchecked1.push_back (i.block->hash ());
	}
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block1->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block2->hash ()) != unchecked1.end ());
	ASSERT_TRUE (std::find (unchecked1.begin (), unchecked1.end (), block3->hash ()) != unchecked1.end ());
	std::vector<btcb::block_hash> unchecked2;
	auto unchecked2_blocks (store.unchecked_get (transaction, block1->hash ()));
	ASSERT_EQ (unchecked2_blocks.size (), 2);
	for (auto & i : unchecked2_blocks)
	{
		unchecked2.push_back (i.block->hash ());
	}
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block1->hash ()) != unchecked2.end ());
	ASSERT_TRUE (std::find (unchecked2.begin (), unchecked2.end (), block2->hash ()) != unchecked2.end ());
	auto unchecked3 (store.unchecked_get (transaction, block2->previous ()));
	ASSERT_EQ (unchecked3.size (), 1);
	ASSERT_EQ (unchecked3[0].block->hash (), block2->hash ());
	auto unchecked4 (store.unchecked_get (transaction, block3->hash ()));
	ASSERT_EQ (unchecked4.size (), 1);
	ASSERT_EQ (unchecked4[0].block->hash (), block3->hash ());
	auto unchecked5 (store.unchecked_get (transaction, block2->hash ()));
	ASSERT_EQ (unchecked5.size (), 0);
}

TEST (block_store, empty_accounts)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin_read ());
	auto begin (store.latest_begin (transaction));
	auto end (store.latest_end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_block)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::open_block block1 (0, 1, 0, btcb::keypair ().prv, 0, 0);
	auto transaction (store.tx_begin_write ());
	btcb::block_sideband sideband (btcb::block_type::open, 0, 0, 0, 0, 0);
	store.block_put (transaction, block1.hash (), block1, sideband);
	ASSERT_TRUE (store.block_exists (transaction, block1.hash ()));
}

TEST (block_store, empty_bootstrap)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin_read ());
	auto begin (store.unchecked_begin (transaction));
	auto end (store.unchecked_end ());
	ASSERT_EQ (end, begin);
}

TEST (block_store, one_bootstrap)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto block1 (std::make_shared<btcb::send_block> (0, 1, 2, btcb::keypair ().prv, 4, 5));
	auto transaction (store.tx_begin_write ());
	store.unchecked_put (transaction, block1->hash (), block1);
	store.flush (transaction);
	auto begin (store.unchecked_begin (transaction));
	auto end (store.unchecked_end ());
	ASSERT_NE (end, begin);
	btcb::uint256_union hash1 (begin->first.key ());
	ASSERT_EQ (block1->hash (), hash1);
	auto blocks (store.unchecked_get (transaction, hash1));
	ASSERT_EQ (1, blocks.size ());
	auto block2 (blocks[0].block);
	ASSERT_EQ (*block1, *block2);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, unchecked_begin_search)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::keypair key0;
	btcb::send_block block1 (0, 1, 2, key0.prv, key0.pub, 3);
	btcb::send_block block2 (5, 6, 7, key0.prv, key0.pub, 8);
}

TEST (block_store, frontier_retrieval)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::account account1 (0);
	btcb::account_info info1 (0, 0, 0, 0, 0, 0, 0, btcb::epoch::epoch_0);
	auto transaction (store.tx_begin_write ());
	store.account_put (transaction, account1, info1);
	btcb::account_info info2;
	store.account_get (transaction, account1, info2);
	ASSERT_EQ (info1, info2);
}

TEST (block_store, one_account)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::account account (0);
	btcb::block_hash hash (0);
	auto transaction (store.tx_begin_write ());
	store.account_put (transaction, account, { hash, account, hash, 42, 100, 200, 20, btcb::epoch::epoch_0 });
	auto begin (store.latest_begin (transaction));
	auto end (store.latest_end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account, btcb::account (begin->first));
	btcb::account_info info (begin->second);
	ASSERT_EQ (hash, info.head);
	ASSERT_EQ (42, info.balance.number ());
	ASSERT_EQ (100, info.modified);
	ASSERT_EQ (200, info.block_count);
	ASSERT_EQ (20, info.confirmation_height);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, two_block)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::open_block block1 (0, 1, 1, btcb::keypair ().prv, 0, 0);
	block1.hashables.account = 1;
	std::vector<btcb::block_hash> hashes;
	std::vector<btcb::open_block> blocks;
	hashes.push_back (block1.hash ());
	blocks.push_back (block1);
	auto transaction (store.tx_begin_write ());
	btcb::block_sideband sideband1 (btcb::block_type::open, 0, 0, 0, 0, 0);
	store.block_put (transaction, hashes[0], block1, sideband1);
	btcb::open_block block2 (0, 1, 2, btcb::keypair ().prv, 0, 0);
	hashes.push_back (block2.hash ());
	blocks.push_back (block2);
	btcb::block_sideband sideband2 (btcb::block_type::open, 0, 0, 0, 0, 0);
	store.block_put (transaction, hashes[1], block2, sideband2);
	ASSERT_TRUE (store.block_exists (transaction, block1.hash ()));
	ASSERT_TRUE (store.block_exists (transaction, block2.hash ()));
}

TEST (block_store, two_account)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::account account1 (1);
	btcb::block_hash hash1 (2);
	btcb::account account2 (3);
	btcb::block_hash hash2 (4);
	auto transaction (store.tx_begin_write ());
	store.account_put (transaction, account1, { hash1, account1, hash1, 42, 100, 300, 20, btcb::epoch::epoch_0 });
	store.account_put (transaction, account2, { hash2, account2, hash2, 84, 200, 400, 30, btcb::epoch::epoch_0 });
	auto begin (store.latest_begin (transaction));
	auto end (store.latest_end ());
	ASSERT_NE (end, begin);
	ASSERT_EQ (account1, btcb::account (begin->first));
	btcb::account_info info1 (begin->second);
	ASSERT_EQ (hash1, info1.head);
	ASSERT_EQ (42, info1.balance.number ());
	ASSERT_EQ (100, info1.modified);
	ASSERT_EQ (300, info1.block_count);
	ASSERT_EQ (20, info1.confirmation_height);
	++begin;
	ASSERT_NE (end, begin);
	ASSERT_EQ (account2, btcb::account (begin->first));
	btcb::account_info info2 (begin->second);
	ASSERT_EQ (hash2, info2.head);
	ASSERT_EQ (84, info2.balance.number ());
	ASSERT_EQ (200, info2.modified);
	ASSERT_EQ (400, info2.block_count);
	ASSERT_EQ (30, info2.confirmation_height);
	++begin;
	ASSERT_EQ (end, begin);
}

TEST (block_store, latest_find)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::account account1 (1);
	btcb::block_hash hash1 (2);
	btcb::account account2 (3);
	btcb::block_hash hash2 (4);
	auto transaction (store.tx_begin_write ());
	store.account_put (transaction, account1, { hash1, account1, hash1, 100, 0, 300, 0, btcb::epoch::epoch_0 });
	store.account_put (transaction, account2, { hash2, account2, hash2, 200, 0, 400, 0, btcb::epoch::epoch_0 });
	auto first (store.latest_begin (transaction));
	auto second (store.latest_begin (transaction));
	++second;
	auto find1 (store.latest_begin (transaction, 1));
	ASSERT_EQ (first, find1);
	auto find2 (store.latest_begin (transaction, 3));
	ASSERT_EQ (second, find2);
	auto find3 (store.latest_begin (transaction, 2));
	ASSERT_EQ (second, find3);
}

TEST (block_store, bad_path)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, boost::filesystem::path ("///"));
	ASSERT_TRUE (init);
}

TEST (block_store, DISABLED_already_open) // File can be shared
{
	auto path (btcb::unique_path ());
	boost::filesystem::create_directories (path.parent_path ());
	btcb::set_secure_perm_directory (path.parent_path ());
	std::ofstream file;
	file.open (path.string ().c_str ());
	ASSERT_TRUE (file.is_open ());
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	ASSERT_TRUE (init);
}

TEST (block_store, roots)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::send_block send_block (0, 1, 2, btcb::keypair ().prv, 4, 5);
	ASSERT_EQ (send_block.hashables.previous, send_block.root ());
	btcb::change_block change_block (0, 1, btcb::keypair ().prv, 3, 4);
	ASSERT_EQ (change_block.hashables.previous, change_block.root ());
	btcb::receive_block receive_block (0, 1, btcb::keypair ().prv, 3, 4);
	ASSERT_EQ (receive_block.hashables.previous, receive_block.root ());
	btcb::open_block open_block (0, 1, 2, btcb::keypair ().prv, 4, 5);
	ASSERT_EQ (open_block.hashables.account, open_block.root ());
}

TEST (block_store, pending_exists)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::pending_key two (2, 0);
	btcb::pending_info pending;
	auto transaction (store.tx_begin_write ());
	store.pending_put (transaction, two, pending);
	btcb::pending_key one (1, 0);
	ASSERT_FALSE (store.pending_exists (transaction, one));
}

TEST (block_store, latest_exists)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::block_hash two (2);
	btcb::account_info info;
	auto transaction (store.tx_begin_write ());
	store.account_put (transaction, two, info);
	btcb::block_hash one (1);
	ASSERT_FALSE (store.account_exists (transaction, one));
}

TEST (block_store, large_iteration)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	std::unordered_set<btcb::account> accounts1;
	for (auto i (0); i < 1000; ++i)
	{
		auto transaction (store.tx_begin_write ());
		btcb::account account;
		btcb::random_pool::generate_block (account.bytes.data (), account.bytes.size ());
		accounts1.insert (account);
		store.account_put (transaction, account, btcb::account_info ());
	}
	std::unordered_set<btcb::account> accounts2;
	btcb::account previous (0);
	auto transaction (store.tx_begin_read ());
	for (auto i (store.latest_begin (transaction, 0)), n (store.latest_end ()); i != n; ++i)
	{
		btcb::account current (i->first);
		assert (current.number () > previous.number ());
		accounts2.insert (current);
		previous = current;
	}
	ASSERT_EQ (accounts1, accounts2);
}

TEST (block_store, frontier)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin_write ());
	btcb::block_hash hash (100);
	btcb::account account (200);
	ASSERT_TRUE (store.frontier_get (transaction, hash).is_zero ());
	store.frontier_put (transaction, hash, account);
	ASSERT_EQ (account, store.frontier_get (transaction, hash));
	store.frontier_del (transaction, hash);
	ASSERT_TRUE (store.frontier_get (transaction, hash).is_zero ());
}

TEST (block_store, block_replace)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::send_block send1 (0, 0, 0, btcb::keypair ().prv, 0, 1);
	btcb::send_block send2 (0, 0, 0, btcb::keypair ().prv, 0, 2);
	auto transaction (store.tx_begin_write ());
	btcb::block_sideband sideband1 (btcb::block_type::send, 0, 0, 0, 0, 0);
	store.block_put (transaction, 0, send1, sideband1);
	btcb::block_sideband sideband2 (btcb::block_type::send, 0, 0, 0, 0, 0);
	store.block_put (transaction, 0, send2, sideband2);
	auto block3 (store.block_get (transaction, 0));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (2, block3->block_work ());
}

TEST (block_store, block_count)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin_write ());
	ASSERT_EQ (0, store.block_count (transaction).sum ());
	btcb::open_block block (0, 1, 0, btcb::keypair ().prv, 0, 0);
	btcb::uint256_union hash1 (block.hash ());
	btcb::block_sideband sideband (btcb::block_type::open, 0, 0, 0, 0, 0);
	store.block_put (transaction, hash1, block, sideband);
	ASSERT_EQ (1, store.block_count (transaction).sum ());
}

TEST (block_store, account_count)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	auto transaction (store.tx_begin_write ());
	ASSERT_EQ (0, store.account_count (transaction));
	btcb::account account (200);
	store.account_put (transaction, account, btcb::account_info ());
	ASSERT_EQ (1, store.account_count (transaction));
}

TEST (block_store, sequence_increment)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::keypair key1;
	btcb::keypair key2;
	auto block1 (std::make_shared<btcb::open_block> (0, 1, 0, btcb::keypair ().prv, 0, 0));
	auto transaction (store.tx_begin_write ());
	auto vote1 (store.vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (1, vote1->sequence);
	auto vote2 (store.vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (2, vote2->sequence);
	auto vote3 (store.vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (1, vote3->sequence);
	auto vote4 (store.vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (2, vote4->sequence);
	vote1->sequence = 20;
	auto seq5 (store.vote_max (transaction, vote1));
	ASSERT_EQ (20, seq5->sequence);
	vote3->sequence = 30;
	auto seq6 (store.vote_max (transaction, vote3));
	ASSERT_EQ (30, seq6->sequence);
	auto vote5 (store.vote_generate (transaction, key1.pub, key1.prv, block1));
	ASSERT_EQ (21, vote5->sequence);
	auto vote6 (store.vote_generate (transaction, key2.pub, key2.prv, block1));
	ASSERT_EQ (31, vote6->sequence);
}

TEST (block_store, upgrade_v2_v3)
{
	btcb::keypair key1;
	btcb::keypair key2;
	btcb::block_hash change_hash;
	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		bool init (false);
		btcb::mdb_store store (init, logger, path);
		ASSERT_TRUE (!init);
		auto transaction (store.tx_begin_write ());
		btcb::genesis genesis;
		auto hash (genesis.hash ());
		store.initialize (transaction, genesis);
		btcb::stat stats;
		btcb::ledger ledger (store, stats);
		btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
		btcb::change_block change (hash, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (hash));
		change_hash = change.hash ();
		ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, change).code);
		ASSERT_EQ (0, ledger.weight (transaction, btcb::test_genesis_key.pub));
		ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, key1.pub));
		store.version_put (transaction, 2);
		store.representation_put (transaction, key1.pub, 7);
		ASSERT_EQ (7, ledger.weight (transaction, key1.pub));
		ASSERT_EQ (2, store.version_get (transaction));
		store.representation_put (transaction, key2.pub, 6);
		ASSERT_EQ (6, ledger.weight (transaction, key2.pub));
		btcb::account_info info;
		ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info));
		info.rep_block = 42;
		btcb::account_info_v5 info_old (info.head, info.rep_block, info.open_block, info.balance, info.modified);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, btcb::mdb_val (btcb::test_genesis_key.pub), info_old.val (), 0));
		assert (status == 0);
	}
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	auto transaction (store.tx_begin_write ());
	ASSERT_TRUE (!init);
	ASSERT_LT (2, store.version_get (transaction));
	ASSERT_EQ (btcb::genesis_amount, ledger.weight (transaction, key1.pub));
	ASSERT_EQ (0, ledger.weight (transaction, key2.pub));
	btcb::account_info info;
	ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info));
	ASSERT_EQ (change_hash, info.rep_block);
}

TEST (block_store, upgrade_v3_v4)
{
	btcb::keypair key1;
	btcb::keypair key2;
	btcb::keypair key3;
	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		bool init (false);
		btcb::mdb_store store (init, logger, path);
		ASSERT_FALSE (init);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 3);
		btcb::pending_info_v3 info (key1.pub, 100, key2.pub);
		auto status (mdb_put (store.env.tx (transaction), store.pending_v0, btcb::mdb_val (key3.pub), info.val (), 0));
		ASSERT_EQ (0, status);
	}
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	btcb::stat stats;
	btcb::ledger ledger (store, stats);
	auto transaction (store.tx_begin_write ());
	ASSERT_FALSE (init);
	ASSERT_LT (3, store.version_get (transaction));
	btcb::pending_key key (key2.pub, key3.pub);
	btcb::pending_info info;
	auto error (store.pending_get (transaction, key, info));
	ASSERT_FALSE (error);
	ASSERT_EQ (key1.pub, info.source);
	ASSERT_EQ (btcb::amount (100), info.amount);
	ASSERT_EQ (btcb::epoch::epoch_0, info.epoch);
}

TEST (block_store, upgrade_v4_v5)
{
	btcb::block_hash genesis_hash (0);
	btcb::block_hash hash (0);
	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		bool init (false);
		btcb::mdb_store store (init, logger, path);
		ASSERT_FALSE (init);
		auto transaction (store.tx_begin_write ());
		btcb::genesis genesis;
		btcb::stat stats;
		btcb::ledger ledger (store, stats);
		store.initialize (transaction, genesis);
		store.version_put (transaction, 4);
		btcb::account_info info;
		ASSERT_FALSE (store.account_get (transaction, btcb::test_genesis_key.pub, info));
		btcb::keypair key0;
		btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
		btcb::send_block block0 (info.head, key0.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (info.head));
		ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block0).code);
		hash = block0.hash ();
		auto original (store.block_get (transaction, info.head));
		genesis_hash = info.head;
		store.block_successor_clear (transaction, info.head);
		ASSERT_TRUE (store.block_successor (transaction, genesis_hash).is_zero ());
		modify_genesis_account_info_to_v5 (store, transaction);
	}
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (hash, store.block_successor (transaction, genesis_hash));
}

TEST (block_store, block_random)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	auto block (store.block_random (transaction));
	ASSERT_NE (nullptr, block);
	ASSERT_EQ (*block, *genesis.open);
}

TEST (block_store, upgrade_v5_v6)
{
	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		bool init (false);
		btcb::mdb_store store (init, logger, path);
		ASSERT_FALSE (init);
		auto transaction (store.tx_begin_write ());
		btcb::genesis genesis;
		store.initialize (transaction, genesis);
		store.version_put (transaction, 5);
		modify_genesis_account_info_to_v5 (store, transaction);
	}
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin_read ());
	btcb::account_info info;
	store.account_get (transaction, btcb::test_genesis_key.pub, info);
	ASSERT_EQ (1, info.block_count);
}

TEST (block_store, upgrade_v6_v7)
{
	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		bool init (false);
		btcb::mdb_store store (init, logger, path);
		ASSERT_FALSE (init);
		auto transaction (store.tx_begin_write ());
		btcb::genesis genesis;
		store.initialize (transaction, genesis);
		store.version_put (transaction, 6);
		modify_account_info_to_v13 (store, transaction, btcb::genesis_account);
		auto send1 (std::make_shared<btcb::send_block> (0, 0, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
		store.unchecked_put (transaction, send1->hash (), send1);
		store.flush (transaction);
		ASSERT_NE (store.unchecked_end (), store.unchecked_begin (transaction));
	}
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.unchecked_end (), store.unchecked_begin (transaction));
}

// Databases need to be dropped in order to convert to dupsort compatible
TEST (block_store, DISABLED_change_dupsort) // Unchecked is no longer dupsort table
{
	auto path (btcb::unique_path ());
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	auto transaction (store.tx_begin_write ());
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &store.unchecked));
	auto send1 (std::make_shared<btcb::send_block> (0, 0, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	auto send2 (std::make_shared<btcb::send_block> (1, 0, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	ASSERT_NE (send1->hash (), send2->hash ());
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 0));
	mdb_dbi_close (store.env, store.unchecked);
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
	ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
	ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE | MDB_DUPSORT, &store.unchecked));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_NE (store.unchecked_end (), iterator1);
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
}

TEST (block_store, upgrade_v7_v8)
{
	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		bool init (false);
		btcb::mdb_store store (init, logger, path);
		auto transaction (store.tx_begin_write ());
		ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.unchecked, 1));
		ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "unchecked", MDB_CREATE, &store.unchecked));
		store.version_put (transaction, 7);
	}
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin_write ());
	auto send1 (std::make_shared<btcb::send_block> (0, 0, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	auto send2 (std::make_shared<btcb::send_block> (1, 0, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	store.unchecked_put (transaction, send1->hash (), send1);
	store.unchecked_put (transaction, send1->hash (), send2);
	store.flush (transaction);
	{
		auto iterator1 (store.unchecked_begin (transaction));
		++iterator1;
		ASSERT_NE (store.unchecked_end (), iterator1);
		++iterator1;
		ASSERT_EQ (store.unchecked_end (), iterator1);
	}
}

TEST (block_store, sequence_flush)
{
	auto path (btcb::unique_path ());
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin_write ());
	btcb::keypair key1;
	auto send1 (std::make_shared<btcb::send_block> (0, 0, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	auto vote1 (store.vote_generate (transaction, key1.pub, key1.prv, send1));
	auto seq2 (store.vote_get (transaction, vote1->account));
	ASSERT_EQ (nullptr, seq2);
	store.flush (transaction);
	auto seq3 (store.vote_get (transaction, vote1->account));
	ASSERT_EQ (*seq3, *vote1);
}

TEST (block_store, sequence_flush_by_hash)
{
	auto path (btcb::unique_path ());
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin_write ());
	btcb::keypair key1;
	std::vector<btcb::block_hash> blocks1;
	blocks1.push_back (btcb::genesis ().hash ());
	blocks1.push_back (1234);
	blocks1.push_back (5678);
	auto vote1 (store.vote_generate (transaction, key1.pub, key1.prv, blocks1));
	auto seq2 (store.vote_get (transaction, vote1->account));
	ASSERT_EQ (nullptr, seq2);
	store.flush (transaction);
	auto seq3 (store.vote_get (transaction, vote1->account));
	ASSERT_EQ (*seq3, *vote1);
}

// Upgrading tracking block sequence numbers to whole vote.
TEST (block_store, upgrade_v8_v9)
{
	auto path (btcb::unique_path ());
	btcb::keypair key;
	{
		btcb::logger_mt logger;
		bool init (false);
		btcb::mdb_store store (init, logger, path);
		auto transaction (store.tx_begin_write ());
		ASSERT_EQ (0, mdb_drop (store.env.tx (transaction), store.vote, 1));
		ASSERT_EQ (0, mdb_dbi_open (store.env.tx (transaction), "sequence", MDB_CREATE, &store.vote));
		uint64_t sequence (10);
		ASSERT_EQ (0, mdb_put (store.env.tx (transaction), store.vote, btcb::mdb_val (key.pub), btcb::mdb_val (sizeof (sequence), &sequence), 0));
		store.version_put (transaction, 8);
	}
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, path);
	ASSERT_FALSE (init);
	auto transaction (store.tx_begin_read ());
	ASSERT_LT (8, store.version_get (transaction));
	auto vote (store.vote_get (transaction, key.pub));
	ASSERT_NE (nullptr, vote);
	ASSERT_EQ (10, vote->sequence);
}

TEST (block_store, state_block)
{
	btcb::logger_mt logger;
	bool error (false);
	btcb::mdb_store store (error, logger, btcb::unique_path ());
	ASSERT_FALSE (error);
	btcb::genesis genesis;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::keypair key1;
	btcb::state_block block1 (1, genesis.hash (), 3, 4, 6, key1.prv, key1.pub, 7);
	ASSERT_EQ (btcb::block_type::state, block1.type ());
	btcb::block_sideband sideband1 (btcb::block_type::state, 0, 0, 0, 0, 0);
	store.block_put (transaction, block1.hash (), block1, sideband1);
	ASSERT_TRUE (store.block_exists (transaction, block1.hash ()));
	auto block2 (store.block_get (transaction, block1.hash ()));
	ASSERT_NE (nullptr, block2);
	ASSERT_EQ (block1, *block2);
	auto count (store.block_count (transaction));
	ASSERT_EQ (1, count.state_v0);
	ASSERT_EQ (0, count.state_v1);
	store.block_del (transaction, block1.hash ());
	ASSERT_FALSE (store.block_exists (transaction, block1.hash ()));
	auto count2 (store.block_count (transaction));
	ASSERT_EQ (0, count2.state_v0);
	ASSERT_EQ (0, count2.state_v1);
}

namespace
{
void write_legacy_sideband (btcb::mdb_store & store_a, btcb::transaction & transaction_a, btcb::block & block_a, btcb::block_hash const & successor_a, MDB_dbi db_a)
{
	std::vector<uint8_t> vector;
	{
		btcb::vectorstream stream (vector);
		block_a.serialize (stream);
		btcb::write (stream, successor_a);
	}
	MDB_val val{ vector.size (), vector.data () };
	auto hash (block_a.hash ());
	auto status2 (mdb_put (store_a.env.tx (transaction_a), db_a, btcb::mdb_val (hash), &val, 0));
	ASSERT_EQ (0, status2);
	btcb::block_sideband sideband;
	auto block2 (store_a.block_get (transaction_a, block_a.hash (), &sideband));
	ASSERT_NE (nullptr, block2);
	ASSERT_EQ (0, sideband.height);
};
}

TEST (block_store, upgrade_sideband_genesis)
{
	bool error (false);
	btcb::genesis genesis;
	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		btcb::mdb_store store (error, logger, path);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		store.initialize (transaction, genesis);
		modify_account_info_to_v13 (store, transaction, btcb::genesis_account);
		btcb::block_sideband sideband;
		auto genesis_block (store.block_get (transaction, genesis.hash (), &sideband));
		ASSERT_NE (nullptr, genesis_block);
		ASSERT_EQ (1, sideband.height);
		write_legacy_sideband (store, transaction, *genesis_block, 0, store.open_blocks);
		auto genesis_block2 (store.block_get (transaction, genesis.hash (), &sideband));
		ASSERT_NE (nullptr, genesis_block);
		ASSERT_EQ (0, sideband.height);
	}
	btcb::logger_mt logger;
	btcb::mdb_store store (error, logger, path);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());
	ASSERT_TRUE (store.full_sideband (transaction));
	btcb::block_sideband sideband;
	auto genesis_block (store.block_get (transaction, genesis.hash (), &sideband));
	ASSERT_NE (nullptr, genesis_block);
	ASSERT_EQ (1, sideband.height);
}

TEST (block_store, upgrade_sideband_two_blocks)
{
	bool error (false);
	btcb::genesis genesis;
	btcb::block_hash hash2;
	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		btcb::mdb_store store (error, logger, path);
		ASSERT_FALSE (error);
		btcb::stat stat;
		btcb::ledger ledger (store, stat);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		store.initialize (transaction, genesis);
		btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
		btcb::state_block block (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
		hash2 = block.hash ();
		ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block).code);
		write_legacy_sideband (store, transaction, *genesis.open, hash2, store.open_blocks);
		write_legacy_sideband (store, transaction, block, 0, store.state_blocks_v0);
		modify_account_info_to_v13 (store, transaction, btcb::genesis_account);
	}
	btcb::logger_mt logger;
	btcb::mdb_store store (error, logger, path);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());
	ASSERT_TRUE (store.full_sideband (transaction));
	btcb::block_sideband sideband;
	auto genesis_block (store.block_get (transaction, genesis.hash (), &sideband));
	ASSERT_NE (nullptr, genesis_block);
	ASSERT_EQ (1, sideband.height);
	btcb::block_sideband sideband2;
	auto block2 (store.block_get (transaction, hash2, &sideband2));
	ASSERT_NE (nullptr, block2);
	ASSERT_EQ (2, sideband2.height);
}

TEST (block_store, upgrade_sideband_two_accounts)
{
	bool error (false);
	btcb::genesis genesis;
	btcb::block_hash hash2;
	btcb::block_hash hash3;
	btcb::keypair key;
	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		btcb::mdb_store store (error, logger, path);
		ASSERT_FALSE (error);
		btcb::stat stat;
		btcb::ledger ledger (store, stat);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		store.initialize (transaction, genesis);
		btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
		btcb::state_block block1 (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
		hash2 = block1.hash ();
		ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
		btcb::state_block block2 (key.pub, 0, btcb::test_genesis_key.pub, btcb::Gbcb_ratio, hash2, key.prv, key.pub, pool.generate (key.pub));
		hash3 = block2.hash ();
		ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block2).code);
		write_legacy_sideband (store, transaction, *genesis.open, hash2, store.open_blocks);
		write_legacy_sideband (store, transaction, block1, 0, store.state_blocks_v0);
		write_legacy_sideband (store, transaction, block2, 0, store.state_blocks_v0);
		modify_account_info_to_v13 (store, transaction, btcb::genesis_account);
		modify_account_info_to_v13 (store, transaction, block2.account ());
	}
	btcb::logger_mt logger;
	btcb::mdb_store store (error, logger, path);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());
	ASSERT_TRUE (store.full_sideband (transaction));
	btcb::block_sideband sideband;
	auto genesis_block (store.block_get (transaction, genesis.hash (), &sideband));
	ASSERT_NE (nullptr, genesis_block);
	ASSERT_EQ (1, sideband.height);
	btcb::block_sideband sideband2;
	auto block2 (store.block_get (transaction, hash2, &sideband2));
	ASSERT_NE (nullptr, block2);
	ASSERT_EQ (2, sideband2.height);
	btcb::block_sideband sideband3;
	auto block3 (store.block_get (transaction, hash3, &sideband3));
	ASSERT_NE (nullptr, block3);
	ASSERT_EQ (1, sideband3.height);
}

TEST (block_store, insert_after_legacy)
{
	btcb::logger_mt logger;
	bool error (false);
	btcb::genesis genesis;
	btcb::mdb_store store (error, logger, btcb::unique_path ());
	ASSERT_FALSE (error);
	btcb::stat stat;
	btcb::ledger ledger (store, stat);
	auto transaction (store.tx_begin_write ());
	store.version_put (transaction, 11);
	store.initialize (transaction, genesis);
	write_legacy_sideband (store, transaction, *genesis.open, 0, store.open_blocks);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::state_block block (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block).code);
}

// Account for an open block should be retrievable
TEST (block_store, legacy_account_computed)
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
	store.version_put (transaction, 11);
	write_legacy_sideband (store, transaction, *genesis.open, 0, store.open_blocks);
	ASSERT_EQ (btcb::genesis_account, ledger.account (transaction, genesis.hash ()));
}

TEST (block_store, upgrade_sideband_epoch)
{
	bool error (false);
	btcb::genesis genesis;
	btcb::block_hash hash2;
	auto path (btcb::unique_path ());
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	{
		btcb::logger_mt logger;
		btcb::mdb_store store (error, logger, path);
		ASSERT_FALSE (error);
		btcb::stat stat;
		btcb::ledger ledger (store, stat, 42, btcb::test_genesis_key.pub);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 11);
		store.initialize (transaction, genesis);
		btcb::state_block block1 (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount, 42, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
		hash2 = block1.hash ();
		ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block1).code);
		ASSERT_EQ (btcb::epoch::epoch_1, store.block_version (transaction, hash2));
		write_legacy_sideband (store, transaction, *genesis.open, hash2, store.open_blocks);
		write_legacy_sideband (store, transaction, block1, 0, store.state_blocks_v1);
		modify_account_info_to_v13 (store, transaction, btcb::genesis_account);
	}
	btcb::logger_mt logger;
	btcb::mdb_store store (error, logger, path);
	btcb::stat stat;
	btcb::ledger ledger (store, stat, 42, btcb::test_genesis_key.pub);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_write ());
	ASSERT_TRUE (store.full_sideband (transaction));
	ASSERT_EQ (btcb::epoch::epoch_1, store.block_version (transaction, hash2));
	btcb::block_sideband sideband;
	auto block1 (store.block_get (transaction, hash2, &sideband));
	ASSERT_NE (0, sideband.height);
	btcb::state_block block2 (btcb::test_genesis_key.pub, hash2, btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (hash2));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, block2).code);
	ASSERT_EQ (btcb::epoch::epoch_1, store.block_version (transaction, block2.hash ()));
}

TEST (block_store, sideband_height)
{
	btcb::logger_mt logger;
	bool error (false);
	btcb::genesis genesis;
	btcb::keypair epoch_key;
	btcb::keypair key1;
	btcb::keypair key2;
	btcb::keypair key3;
	btcb::mdb_store store (error, logger, btcb::unique_path ());
	ASSERT_FALSE (error);
	btcb::stat stat;
	btcb::ledger ledger (store, stat);
	ledger.epoch_signer = epoch_key.pub;
	auto transaction (store.tx_begin_write ());
	store.initialize (transaction, genesis);
	btcb::work_pool pool (std::numeric_limits<unsigned>::max ());
	btcb::send_block send (genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (genesis.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send).code);
	btcb::receive_block receive (send.hash (), send.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (send.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive).code);
	btcb::change_block change (receive.hash (), 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (receive.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, change).code);
	btcb::state_block state_send1 (btcb::test_genesis_key.pub, change.hash (), 0, btcb::genesis_amount - btcb::Gbcb_ratio, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (change.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, state_send1).code);
	btcb::state_block state_send2 (btcb::test_genesis_key.pub, state_send1.hash (), 0, btcb::genesis_amount - 2 * btcb::Gbcb_ratio, key2.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (state_send1.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, state_send2).code);
	btcb::state_block state_send3 (btcb::test_genesis_key.pub, state_send2.hash (), 0, btcb::genesis_amount - 3 * btcb::Gbcb_ratio, key3.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, pool.generate (state_send2.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, state_send3).code);
	btcb::state_block state_open (key1.pub, 0, 0, btcb::Gbcb_ratio, state_send1.hash (), key1.prv, key1.pub, pool.generate (key1.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, state_open).code);
	btcb::state_block epoch (key1.pub, state_open.hash (), 0, btcb::Gbcb_ratio, ledger.epoch_link, epoch_key.prv, epoch_key.pub, pool.generate (state_open.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, epoch).code);
	ASSERT_EQ (btcb::epoch::epoch_1, store.block_version (transaction, epoch.hash ()));
	btcb::state_block epoch_open (key2.pub, 0, 0, 0, ledger.epoch_link, epoch_key.prv, epoch_key.pub, pool.generate (key2.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, epoch_open).code);
	ASSERT_EQ (btcb::epoch::epoch_1, store.block_version (transaction, epoch_open.hash ()));
	btcb::state_block state_receive (key2.pub, epoch_open.hash (), 0, btcb::Gbcb_ratio, state_send2.hash (), key2.prv, key2.pub, pool.generate (epoch_open.hash ()));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, state_receive).code);
	btcb::open_block open (state_send3.hash (), btcb::test_genesis_key.pub, key3.pub, key3.prv, key3.pub, pool.generate (key3.pub));
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open).code);
	btcb::block_sideband sideband1;
	auto block1 (store.block_get (transaction, genesis.hash (), &sideband1));
	ASSERT_EQ (sideband1.height, 1);
	btcb::block_sideband sideband2;
	auto block2 (store.block_get (transaction, send.hash (), &sideband2));
	ASSERT_EQ (sideband2.height, 2);
	btcb::block_sideband sideband3;
	auto block3 (store.block_get (transaction, receive.hash (), &sideband3));
	ASSERT_EQ (sideband3.height, 3);
	btcb::block_sideband sideband4;
	auto block4 (store.block_get (transaction, change.hash (), &sideband4));
	ASSERT_EQ (sideband4.height, 4);
	btcb::block_sideband sideband5;
	auto block5 (store.block_get (transaction, state_send1.hash (), &sideband5));
	ASSERT_EQ (sideband5.height, 5);
	btcb::block_sideband sideband6;
	auto block6 (store.block_get (transaction, state_send2.hash (), &sideband6));
	ASSERT_EQ (sideband6.height, 6);
	btcb::block_sideband sideband7;
	auto block7 (store.block_get (transaction, state_send3.hash (), &sideband7));
	ASSERT_EQ (sideband7.height, 7);
	btcb::block_sideband sideband8;
	auto block8 (store.block_get (transaction, state_open.hash (), &sideband8));
	ASSERT_EQ (sideband8.height, 1);
	btcb::block_sideband sideband9;
	auto block9 (store.block_get (transaction, epoch.hash (), &sideband9));
	ASSERT_EQ (sideband9.height, 2);
	btcb::block_sideband sideband10;
	auto block10 (store.block_get (transaction, epoch_open.hash (), &sideband10));
	ASSERT_EQ (sideband10.height, 1);
	btcb::block_sideband sideband11;
	auto block11 (store.block_get (transaction, state_receive.hash (), &sideband11));
	ASSERT_EQ (sideband11.height, 2);
	btcb::block_sideband sideband12;
	auto block12 (store.block_get (transaction, open.hash (), &sideband12));
	ASSERT_EQ (sideband12.height, 1);
}

TEST (block_store, peers)
{
	btcb::logger_mt logger;
	auto init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);

	auto transaction (store.tx_begin_write ());
	btcb::endpoint_key endpoint (boost::asio::ip::address_v6::any ().to_bytes (), 100);

	// Confirm that the store is empty
	ASSERT_FALSE (store.peer_exists (transaction, endpoint));
	ASSERT_EQ (store.peer_count (transaction), 0);

	// Add one, confirm that it can be found
	store.peer_put (transaction, endpoint);
	ASSERT_TRUE (store.peer_exists (transaction, endpoint));
	ASSERT_EQ (store.peer_count (transaction), 1);

	// Add another one and check that it (and the existing one) can be found
	btcb::endpoint_key endpoint1 (boost::asio::ip::address_v6::any ().to_bytes (), 101);
	store.peer_put (transaction, endpoint1);
	ASSERT_TRUE (store.peer_exists (transaction, endpoint1)); // Check new peer is here
	ASSERT_TRUE (store.peer_exists (transaction, endpoint)); // Check first peer is still here
	ASSERT_EQ (store.peer_count (transaction), 2);

	// Delete the first one
	store.peer_del (transaction, endpoint1);
	ASSERT_FALSE (store.peer_exists (transaction, endpoint1)); // Confirm it no longer exists
	ASSERT_TRUE (store.peer_exists (transaction, endpoint)); // Check first peer is still here
	ASSERT_EQ (store.peer_count (transaction), 1);

	// Delete original one
	store.peer_del (transaction, endpoint);
	ASSERT_EQ (store.peer_count (transaction), 0);
	ASSERT_FALSE (store.peer_exists (transaction, endpoint));
}

TEST (block_store, endpoint_key_byte_order)
{
	boost::asio::ip::address_v6 address (boost::asio::ip::address_v6::from_string ("::ffff:127.0.0.1"));
	auto port = 100;
	btcb::endpoint_key endpoint_key (address.to_bytes (), port);

	std::vector<uint8_t> bytes;
	{
		btcb::vectorstream stream (bytes);
		btcb::write (stream, endpoint_key);
	}

	// This checks that the endpoint is serialized as expected, with a size
	// of 18 bytes (16 for ipv6 address and 2 for port), both in network byte order.
	ASSERT_EQ (bytes.size (), 18);
	ASSERT_EQ (bytes[10], 0xff);
	ASSERT_EQ (bytes[11], 0xff);
	ASSERT_EQ (bytes[12], 127);
	ASSERT_EQ (bytes[bytes.size () - 2], 0);
	ASSERT_EQ (bytes.back (), 100);

	// Deserialize the same stream bytes
	btcb::bufferstream stream1 (bytes.data (), bytes.size ());
	btcb::endpoint_key endpoint_key1;
	btcb::read (stream1, endpoint_key1);

	// This should be in network bytes order
	ASSERT_EQ (address.to_bytes (), endpoint_key1.address_bytes ());

	// This should be in host byte order
	ASSERT_EQ (port, endpoint_key1.port ());
}

TEST (block_store, online_weight)
{
	btcb::logger_mt logger;
	bool error (false);
	btcb::mdb_store store (error, logger, btcb::unique_path ());
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_write ());
	ASSERT_EQ (0, store.online_weight_count (transaction));
	ASSERT_EQ (store.online_weight_end (), store.online_weight_begin (transaction));
	store.online_weight_put (transaction, 1, 2);
	ASSERT_EQ (1, store.online_weight_count (transaction));
	auto item (store.online_weight_begin (transaction));
	ASSERT_NE (store.online_weight_end (), item);
	ASSERT_EQ (1, item->first);
	ASSERT_EQ (2, item->second.number ());
	store.online_weight_del (transaction, 1);
	ASSERT_EQ (0, store.online_weight_count (transaction));
	ASSERT_EQ (store.online_weight_end (), store.online_weight_begin (transaction));
}

// Adding confirmation height to accounts
TEST (block_store, upgrade_v13_v14)
{
	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		btcb::genesis genesis;
		auto error (false);
		btcb::mdb_store store (error, logger, path);
		auto transaction (store.tx_begin_write ());
		store.initialize (transaction, genesis);
		btcb::account_info account_info;
		ASSERT_FALSE (store.account_get (transaction, btcb::genesis_account, account_info));
		ASSERT_EQ (account_info.confirmation_height, 1);
		store.version_put (transaction, 13);
		modify_account_info_to_v13 (store, transaction, btcb::genesis_account);

		// This should fail as sizes are no longer correct for account_info
		btcb::account_info account_info1;
		ASSERT_TRUE (store.account_get (transaction, btcb::genesis_account, account_info1));
	}

	// Now do the upgrade and confirm that confirmation height is 0 and version is updated as expected
	btcb::logger_mt logger;
	auto error (false);
	btcb::mdb_store store (error, logger, path);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_write ());

	// This should now work and have confirmation height of 0
	btcb::account_info account_info;
	ASSERT_FALSE (store.account_get (transaction, btcb::genesis_account, account_info));
	ASSERT_EQ (account_info.confirmation_height, 0);
	ASSERT_LT (13, store.version_get (transaction));

	// Test deleting node ID
	btcb::uint256_union node_id_mdb_key (3);
	btcb::mdb_val value;
	auto error_node_id (mdb_get (store.env.tx (transaction), store.meta, btcb::mdb_val (node_id_mdb_key), value));
	ASSERT_EQ (error_node_id, MDB_NOTFOUND);
}

// Test various confirmation height values as well as clearing them
TEST (block_store, confirmation_height)
{
	auto path (btcb::unique_path ());
	btcb::logger_mt logger;
	auto error (false);
	btcb::mdb_store store (error, logger, path);
	auto transaction (store.tx_begin_write ());

	btcb::account account1 (0);
	btcb::account_info info1 (0, 0, 0, 0, 0, 0, 500, btcb::epoch::epoch_0);
	store.account_put (transaction, account1, info1);

	btcb::account account2 (1);
	btcb::account_info info2 (0, 0, 0, 0, 0, 0, std::numeric_limits<uint64_t>::max (), btcb::epoch::epoch_0);
	store.account_put (transaction, account2, info2);

	btcb::account account3 (2);
	btcb::account_info info3 (0, 0, 0, 0, 0, 0, 10, btcb::epoch::epoch_0);
	store.account_put (transaction, account3, info3);

	btcb::account_info stored_account_info;
	ASSERT_FALSE (store.account_get (transaction, account1, stored_account_info));
	ASSERT_EQ (stored_account_info.confirmation_height, 500);

	ASSERT_FALSE (store.account_get (transaction, account2, stored_account_info));
	ASSERT_EQ (stored_account_info.confirmation_height, std::numeric_limits<uint64_t>::max ());

	ASSERT_FALSE (store.account_get (transaction, account3, stored_account_info));
	ASSERT_EQ (stored_account_info.confirmation_height, 10);

	// Check cleaning of confirmation heights
	store.confirmation_height_clear (transaction);
	ASSERT_EQ (store.account_count (transaction), 3);

	ASSERT_FALSE (store.account_get (transaction, account1, stored_account_info));
	ASSERT_EQ (stored_account_info.confirmation_height, 0);

	ASSERT_FALSE (store.account_get (transaction, account2, stored_account_info));
	ASSERT_EQ (stored_account_info.confirmation_height, 0);

	ASSERT_FALSE (store.account_get (transaction, account3, stored_account_info));
	ASSERT_EQ (stored_account_info.confirmation_height, 0);
}

// Upgrade many accounts to add a confirmation height of 0
TEST (block_store, upgrade_confirmation_height_many)
{
	auto error (false);
	btcb::genesis genesis;
	auto total_num_accounts = 1000; // Includes the genesis account

	auto path (btcb::unique_path ());
	{
		btcb::logger_mt logger;
		btcb::mdb_store store (error, logger, path);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, 13);
		store.initialize (transaction, genesis);
		modify_account_info_to_v13 (store, transaction, btcb::genesis_account);

		// Add many accounts
		for (auto i = 0; i < total_num_accounts - 1; ++i)
		{
			btcb::account account (i);
			btcb::open_block open (1, 2, 3, nullptr);
			btcb::account_info_v13 account_info_v13 (open.hash (), open.hash (), open.hash (), 3, 4, 1, btcb::epoch::epoch_1);
			auto status (mdb_put (store.env.tx (transaction), store.accounts_v1, btcb::mdb_val (account), btcb::mdb_val (account_info_v13), 0));
			ASSERT_EQ (status, 0);
		}

		ASSERT_EQ (store.account_count (transaction), total_num_accounts);
	}

	// Loop over them all and confirm all have a confirmation height of 0
	btcb::logger_mt logger;
	btcb::mdb_store store (error, logger, path);
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.account_count (transaction), total_num_accounts);

	for (auto i (store.latest_begin (transaction)), n (store.latest_end ()); i != n; ++i)
	{
		btcb::account_info current (i->second);
		ASSERT_EQ (current.confirmation_height, 0);
	}
}

// Ledger versions are not forward compatible
TEST (block_store, incompatible_version)
{
	auto path (btcb::unique_path ());
	btcb::logger_mt logger;
	{
		auto error (false);
		btcb::mdb_store store (error, logger, path);
		ASSERT_FALSE (error);

		// Put version to an unreachable number so that it should always be incompatible
		auto transaction (store.tx_begin_write ());
		store.version_put (transaction, std::numeric_limits<unsigned>::max ());
	}

	// Now try and read it, should give an error
	{
		auto error (false);
		btcb::mdb_store store (error, logger, path);
		ASSERT_TRUE (error);
	}
}

TEST (block_store, reset_renew_existing_transaction)
{
	btcb::logger_mt logger;
	bool init (false);
	btcb::mdb_store store (init, logger, btcb::unique_path ());
	ASSERT_TRUE (!init);

	btcb::keypair key1;
	btcb::open_block block (0, 1, 1, btcb::keypair ().prv, 0, 0);
	btcb::uint256_union hash1 (block.hash ());
	auto read_transaction = store.tx_begin_read ();

	// Block shouldn't exist yet
	auto block_non_existing (store.block_get (read_transaction, hash1));
	ASSERT_EQ (nullptr, block_non_existing);

	// Release resources for the transaction
	read_transaction.reset ();

	// Write the block
	{
		auto write_transaction (store.tx_begin_write ());
		btcb::block_sideband sideband (btcb::block_type::open, 0, 0, 0, 0, 0);
		store.block_put (write_transaction, hash1, block, sideband);
	}

	read_transaction.renew ();

	// Block should exist now
	auto block_existing (store.block_get (read_transaction, hash1));
	ASSERT_NE (nullptr, block_existing);
}

namespace
{
// These functions take the latest account_info and create a legacy one so that upgrade tests can be emulated more easily.
void modify_account_info_to_v13 (btcb::mdb_store & store, btcb::transaction const & transaction_a, btcb::account const & account)
{
	btcb::account_info info;
	ASSERT_FALSE (store.account_get (transaction_a, account, info));
	btcb::account_info_v13 account_info_v13 (info.head, info.rep_block, info.open_block, info.balance, info.modified, info.block_count, info.epoch);
	auto status (mdb_put (store.env.tx (transaction_a), store.get_account_db (info.epoch), btcb::mdb_val (account), btcb::mdb_val (account_info_v13), 0));
	assert (status == 0);
}

void modify_genesis_account_info_to_v5 (btcb::mdb_store & store, btcb::transaction const & transaction_a)
{
	btcb::account_info info;
	store.account_get (transaction_a, btcb::test_genesis_key.pub, info);
	btcb::account_info_v5 info_old (info.head, info.rep_block, info.open_block, info.balance, info.modified);
	auto status (mdb_put (store.env.tx (transaction_a), store.accounts_v0, btcb::mdb_val (btcb::test_genesis_key.pub), info_old.val (), 0));
	assert (status == 0);
}
}
