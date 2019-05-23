#include <gtest/gtest.h>

#include <btcb/secure/blockstore.hpp>
#include <btcb/secure/versioning.hpp>

TEST (versioning, account_info_v1)
{
	auto file (btcb::unique_path ());
	btcb::account account (1);
	btcb::open_block open (1, 2, 3, nullptr);
	btcb::account_info_v1 v1 (open.hash (), open.hash (), 3, 4);
	{
		btcb::logger_mt logger;
		auto error (false);
		btcb::mdb_store store (error, logger, file);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_write ());
		btcb::block_sideband sideband (btcb::block_type::open, 0, 0, 0, 0, 0);
		store.block_put (transaction, open.hash (), open, sideband);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, btcb::mdb_val (account), v1.val (), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 1);
	}

	btcb::logger_mt logger;
	auto error (false);
	btcb::mdb_store store (error, logger, file);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());
	btcb::account_info v_latest;
	ASSERT_FALSE (store.account_get (transaction, account, v_latest));
	ASSERT_EQ (open.hash (), v_latest.open_block);
	ASSERT_EQ (v1.balance, v_latest.balance);
	ASSERT_EQ (v1.head, v_latest.head);
	ASSERT_EQ (v1.modified, v_latest.modified);
	ASSERT_EQ (v1.rep_block, v_latest.rep_block);
	ASSERT_EQ (1, v_latest.block_count);
	ASSERT_EQ (0, v_latest.confirmation_height);
	ASSERT_EQ (btcb::epoch::epoch_0, v_latest.epoch);
}

TEST (versioning, account_info_v5)
{
	auto file (btcb::unique_path ());
	btcb::account account (1);
	btcb::open_block open (1, 2, 3, nullptr);
	btcb::account_info_v5 v5 (open.hash (), open.hash (), open.hash (), 3, 4);
	{
		btcb::logger_mt logger;
		auto error (false);
		btcb::mdb_store store (error, logger, file);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_write ());
		btcb::block_sideband sideband (btcb::block_type::open, 0, 0, 0, 0, 0);
		store.block_put (transaction, open.hash (), open, sideband);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, btcb::mdb_val (account), v5.val (), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 5);
	}

	btcb::logger_mt logger;
	auto error (false);
	btcb::mdb_store store (error, logger, file);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());
	btcb::account_info v_latest;
	ASSERT_FALSE (store.account_get (transaction, account, v_latest));
	ASSERT_EQ (v5.open_block, v_latest.open_block);
	ASSERT_EQ (v5.balance, v_latest.balance);
	ASSERT_EQ (v5.head, v_latest.head);
	ASSERT_EQ (v5.modified, v_latest.modified);
	ASSERT_EQ (v5.rep_block, v_latest.rep_block);
	ASSERT_EQ (1, v_latest.block_count);
	ASSERT_EQ (0, v_latest.confirmation_height);
	ASSERT_EQ (btcb::epoch::epoch_0, v_latest.epoch);
}

TEST (versioning, account_info_v13)
{
	auto file (btcb::unique_path ());
	btcb::account account (1);
	btcb::open_block open (1, 2, 3, nullptr);
	btcb::account_info_v13 v13 (open.hash (), open.hash (), open.hash (), 3, 4, 10, btcb::epoch::epoch_0);
	{
		btcb::logger_mt logger;
		auto error (false);
		btcb::mdb_store store (error, logger, file);
		ASSERT_FALSE (error);
		auto transaction (store.tx_begin_write ());
		btcb::block_sideband sideband (btcb::block_type::open, 0, 0, 0, 0, 0);
		store.block_put (transaction, open.hash (), open, sideband);
		auto status (mdb_put (store.env.tx (transaction), store.accounts_v0, btcb::mdb_val (account), btcb::mdb_val (v13), 0));
		ASSERT_EQ (0, status);
		store.version_put (transaction, 13);
	}

	btcb::logger_mt logger;
	auto error (false);
	btcb::mdb_store store (error, logger, file);
	ASSERT_FALSE (error);
	auto transaction (store.tx_begin_read ());
	btcb::account_info v_latest;
	ASSERT_FALSE (store.account_get (transaction, account, v_latest));
	ASSERT_EQ (v13.open_block, v_latest.open_block);
	ASSERT_EQ (v13.balance, v_latest.balance);
	ASSERT_EQ (v13.head, v_latest.head);
	ASSERT_EQ (v13.modified, v_latest.modified);
	ASSERT_EQ (v13.rep_block, v_latest.rep_block);
	ASSERT_EQ (v13.block_count, v_latest.block_count);
	ASSERT_EQ (0, v_latest.confirmation_height);
	ASSERT_EQ (v13.epoch, v_latest.epoch);
}
