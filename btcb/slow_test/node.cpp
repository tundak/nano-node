#include <gtest/gtest.h>
#include <btcb/core_test/testutil.hpp>
#include <btcb/crypto_lib/random_pool.hpp>
#include <btcb/node/testing.hpp>
#include <btcb/node/transport/udp.hpp>

#include <thread>

using namespace std::chrono_literals;

TEST (system, generate_mass_activity)
{
	btcb::system system (24000, 1);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	uint32_t count (20);
	system.generate_mass_activity (count, *system.nodes[0]);
	size_t accounts (0);
	auto transaction (system.nodes[0]->store.tx_begin_read ());
	for (auto i (system.nodes[0]->store.latest_begin (transaction)), n (system.nodes[0]->store.latest_end ()); i != n; ++i)
	{
		++accounts;
	}
}

TEST (system, generate_mass_activity_long)
{
	btcb::system system (24000, 1);
	btcb::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	uint32_t count (1000000000);
	system.generate_mass_activity (count, *system.nodes[0]);
	size_t accounts (0);
	auto transaction (system.nodes[0]->store.tx_begin_read ());
	for (auto i (system.nodes[0]->store.latest_begin (transaction)), n (system.nodes[0]->store.latest_end ()); i != n; ++i)
	{
		++accounts;
	}
	system.stop ();
	runner.join ();
}

TEST (system, receive_while_synchronizing)
{
	std::vector<boost::thread> threads;
	{
		btcb::system system (24000, 1);
		btcb::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
		system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
		uint32_t count (1000);
		system.generate_mass_activity (count, *system.nodes[0]);
		btcb::keypair key;
		btcb::node_init init1;
		auto node1 (std::make_shared<btcb::node> (init1, system.io_ctx, 24001, btcb::unique_path (), system.alarm, system.logging, system.work));
		ASSERT_FALSE (init1.error ());
		auto channel (std::make_shared<btcb::transport::channel_udp> (node1->network.udp_channels, system.nodes[0]->network.endpoint ()));
		node1->network.send_keepalive (channel);
		auto wallet (node1->wallets.create (1));
		ASSERT_EQ (key.pub, wallet->insert_adhoc (key.prv));
		node1->start ();
		system.nodes.push_back (node1);
		system.alarm.add (std::chrono::steady_clock::now () + std::chrono::milliseconds (200), ([&system, &key]() {
			auto hash (system.wallet (0)->send_sync (btcb::test_genesis_key.pub, key.pub, system.nodes[0]->config.receive_minimum.number ()));
			auto transaction (system.nodes[0]->store.tx_begin_read ());
			auto block (system.nodes[0]->store.block_get (transaction, hash));
			std::string block_text;
			block->serialize_json (block_text);
		}));
		while (node1->balance (key.pub).is_zero ())
		{
			system.poll ();
		}
		node1->stop ();
		system.stop ();
		runner.join ();
	}
	for (auto i (threads.begin ()), n (threads.end ()); i != n; ++i)
	{
		i->join ();
	}
}

TEST (ledger, deep_account_compute)
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
	btcb::keypair key;
	auto balance (btcb::genesis_amount - 1);
	btcb::send_block send (genesis.hash (), key.pub, balance, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send).code);
	btcb::open_block open (send.hash (), btcb::test_genesis_key.pub, key.pub, key.prv, key.pub, 0);
	ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, open).code);
	auto sprevious (send.hash ());
	auto rprevious (open.hash ());
	for (auto i (0), n (100000); i != n; ++i)
	{
		balance -= 1;
		btcb::send_block send (sprevious, key.pub, balance, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
		ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, send).code);
		sprevious = send.hash ();
		btcb::receive_block receive (rprevious, send.hash (), key.prv, key.pub, 0);
		ASSERT_EQ (btcb::process_result::progress, ledger.process (transaction, receive).code);
		rprevious = receive.hash ();
		if (i % 100 == 0)
		{
			std::cerr << i << ' ';
		}
		auto account (ledger.account (transaction, sprevious));
		(void)account;
		auto balance (ledger.balance (transaction, rprevious));
		(void)balance;
	}
}

TEST (wallet, multithreaded_send_async)
{
	std::vector<boost::thread> threads;
	{
		btcb::system system (24000, 1);
		btcb::keypair key;
		auto wallet_l (system.wallet (0));
		wallet_l->insert_adhoc (btcb::test_genesis_key.prv);
		wallet_l->insert_adhoc (key.prv);
		for (auto i (0); i < 20; ++i)
		{
			threads.push_back (boost::thread ([wallet_l, &key]() {
				for (auto i (0); i < 1000; ++i)
				{
					wallet_l->send_async (btcb::test_genesis_key.pub, key.pub, 1000, [](std::shared_ptr<btcb::block> block_a) {
						ASSERT_FALSE (block_a == nullptr);
						ASSERT_FALSE (block_a->hash ().is_zero ());
					});
				}
			}));
		}
		system.deadline_set (1000s);
		while (system.nodes[0]->balance (btcb::test_genesis_key.pub) != (btcb::genesis_amount - 20 * 1000 * 1000))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}
	for (auto i (threads.begin ()), n (threads.end ()); i != n; ++i)
	{
		i->join ();
	}
}

TEST (store, load)
{
	btcb::system system (24000, 1);
	std::vector<boost::thread> threads;
	for (auto i (0); i < 100; ++i)
	{
		threads.push_back (boost::thread ([&system]() {
			for (auto i (0); i != 1000; ++i)
			{
				auto transaction (system.nodes[0]->store.tx_begin_write ());
				for (auto j (0); j != 10; ++j)
				{
					btcb::block_hash hash;
					btcb::random_pool::generate_block (hash.bytes.data (), hash.bytes.size ());
					system.nodes[0]->store.account_put (transaction, hash, btcb::account_info ());
				}
			}
		}));
	}
	for (auto & i : threads)
	{
		i.join ();
	}
}

TEST (node, fork_storm)
{
	btcb::system system (24000, 64);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	auto previous (system.nodes[0]->latest (btcb::test_genesis_key.pub));
	auto balance (system.nodes[0]->balance (btcb::test_genesis_key.pub));
	ASSERT_FALSE (previous.is_zero ());
	for (auto j (0); j != system.nodes.size (); ++j)
	{
		balance -= 1;
		btcb::keypair key;
		btcb::send_block send (previous, key.pub, balance, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
		previous = send.hash ();
		for (auto i (0); i != system.nodes.size (); ++i)
		{
			auto send_result (system.nodes[i]->process (send));
			ASSERT_EQ (btcb::process_result::progress, send_result.code);
			btcb::keypair rep;
			auto open (std::make_shared<btcb::open_block> (previous, rep.pub, key.pub, key.prv, key.pub, 0));
			system.nodes[i]->work_generate_blocking (*open);
			auto open_result (system.nodes[i]->process (*open));
			ASSERT_EQ (btcb::process_result::progress, open_result.code);
			auto transaction (system.nodes[i]->store.tx_begin_read ());
			system.nodes[i]->network.flood_block (open);
		}
	}
	auto again (true);

	int empty (0);
	int single (0);
	int iteration (0);
	while (again)
	{
		empty = 0;
		single = 0;
		std::for_each (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<btcb::node> const & node_a) {
			if (node_a->active.empty ())
			{
				++empty;
			}
			else
			{
				if (node_a->active.roots.begin ()->election->last_votes_size () == 1)
				{
					++single;
				}
			}
		});
		system.poll ();
		if ((iteration & 0xff) == 0)
		{
			std::cerr << "Empty: " << empty << " single: " << single << std::endl;
		}
		again = empty != 0 || single != 0;
		++iteration;
	}
	ASSERT_TRUE (true);
}

namespace
{
size_t heard_count (std::vector<uint8_t> const & nodes)
{
	auto result (0);
	for (auto i (nodes.begin ()), n (nodes.end ()); i != n; ++i)
	{
		switch (*i)
		{
			case 0:
				break;
			case 1:
				++result;
				break;
			case 2:
				++result;
				break;
		}
	}
	return result;
}
}

TEST (broadcast, world_broadcast_simulate)
{
	auto node_count (10000);
	// 0 = starting state
	// 1 = heard transaction
	// 2 = repeated transaction
	std::vector<uint8_t> nodes;
	nodes.resize (node_count, 0);
	nodes[0] = 1;
	auto any_changed (true);
	auto message_count (0);
	while (any_changed)
	{
		any_changed = false;
		for (auto i (nodes.begin ()), n (nodes.end ()); i != n; ++i)
		{
			switch (*i)
			{
				case 0:
					break;
				case 1:
					for (auto j (nodes.begin ()), m (nodes.end ()); j != m; ++j)
					{
						++message_count;
						switch (*j)
						{
							case 0:
								*j = 1;
								any_changed = true;
								break;
							case 1:
								break;
							case 2:
								break;
						}
					}
					*i = 2;
					any_changed = true;
					break;
				case 2:
					break;
				default:
					assert (false);
					break;
			}
		}
	}
	auto count (heard_count (nodes));
	(void)count;
}

TEST (broadcast, sqrt_broadcast_simulate)
{
	auto node_count (200);
	auto broadcast_count (std::ceil (std::sqrt (node_count)));
	// 0 = starting state
	// 1 = heard transaction
	// 2 = repeated transaction
	std::vector<uint8_t> nodes;
	nodes.resize (node_count, 0);
	nodes[0] = 1;
	auto any_changed (true);
	uint64_t message_count (0);
	while (any_changed)
	{
		any_changed = false;
		for (auto i (nodes.begin ()), n (nodes.end ()); i != n; ++i)
		{
			switch (*i)
			{
				case 0:
					break;
				case 1:
					for (auto j (0); j != broadcast_count; ++j)
					{
						++message_count;
						auto entry (btcb::random_pool::generate_word32 (0, node_count - 1));
						switch (nodes[entry])
						{
							case 0:
								nodes[entry] = 1;
								any_changed = true;
								break;
							case 1:
								break;
							case 2:
								break;
						}
					}
					*i = 2;
					any_changed = true;
					break;
				case 2:
					break;
				default:
					assert (false);
					break;
			}
		}
	}
	auto count (heard_count (nodes));
	(void)count;
}

TEST (peer_container, random_set)
{
	btcb::system system (24000, 1);
	auto old (std::chrono::steady_clock::now ());
	auto current (std::chrono::steady_clock::now ());
	for (auto i (0); i < 10000; ++i)
	{
		auto list (system.nodes[0]->network.random_set (15));
	}
	auto end (std::chrono::steady_clock::now ());
	(void)end;
	auto old_ms (std::chrono::duration_cast<std::chrono::milliseconds> (current - old));
	(void)old_ms;
	auto new_ms (std::chrono::duration_cast<std::chrono::milliseconds> (end - current));
	(void)new_ms;
}

TEST (store, unchecked_load)
{
	btcb::system system (24000, 1);
	auto & node (*system.nodes[0]);
	auto block (std::make_shared<btcb::send_block> (0, 0, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	for (auto i (0); i < 1000000; ++i)
	{
		auto transaction (node.store.tx_begin_write ());
		node.store.unchecked_put (transaction, i, block);
	}
	auto transaction (node.store.tx_begin_read ());
	auto count (node.store.unchecked_count (transaction));
	(void)count;
}

TEST (store, vote_load)
{
	btcb::system system (24000, 1);
	auto & node (*system.nodes[0]);
	auto block (std::make_shared<btcb::send_block> (0, 0, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	for (auto i (0); i < 1000000; ++i)
	{
		auto vote (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, i, block));
		node.vote_processor.vote (vote, std::make_shared<btcb::transport::channel_udp> (system.nodes[0]->network.udp_channels, system.nodes[0]->network.endpoint ()));
	}
}

TEST (wallets, rep_scan)
{
	btcb::system system (24000, 1);
	auto & node (*system.nodes[0]);
	auto wallet (system.wallet (0));
	{
		auto transaction (node.wallets.tx_begin_write ());
		for (auto i (0); i < 10000; ++i)
		{
			wallet->deterministic_insert (transaction);
		}
	}
	auto transaction (node.store.tx_begin_read ());
	auto begin (std::chrono::steady_clock::now ());
	node.wallets.foreach_representative (transaction, [](btcb::public_key const & pub_a, btcb::raw_key const & prv_a) {
	});
	ASSERT_LT (std::chrono::steady_clock::now () - begin, std::chrono::milliseconds (5));
}

TEST (node, mass_vote_by_hash)
{
	btcb::system system (24000, 1);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::genesis genesis;
	btcb::block_hash previous (genesis.hash ());
	btcb::keypair key;
	std::vector<std::shared_ptr<btcb::state_block>> blocks;
	for (auto i (0); i < 10000; ++i)
	{
		auto block (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, previous, btcb::test_genesis_key.pub, btcb::genesis_amount - (i + 1) * btcb::Gbcb_ratio, key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (previous)));
		previous = block->hash ();
		blocks.push_back (block);
	}
	for (auto i (blocks.begin ()), n (blocks.end ()); i != n; ++i)
	{
		system.nodes[0]->block_processor.add (*i, btcb::seconds_since_epoch ());
	}
}

TEST (confirmation_height, many_accounts)
{
	bool delay_frontier_confirmation_height_updating = true;
	btcb::system system;
	btcb::node_config node_config (24000, system.logging);
	node_config.online_weight_minimum = 100;
	auto node = system.add_node (node_config, delay_frontier_confirmation_height_updating);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);

	// The number of frontiers should be more than the batch_write_size to test the amount of blocks confirmed is correct.
	auto num_accounts = btcb::confirmation_height_processor::batch_write_size * 2 + 50;
	btcb::keypair last_keypair = btcb::test_genesis_key;
	auto last_open_hash = node->latest (btcb::test_genesis_key.pub);
	{
		auto transaction = node->store.tx_begin_write ();
		for (auto i = num_accounts - 1; i > 0; --i)
		{
			btcb::keypair key;
			system.wallet (0)->insert_adhoc (key.prv);

			btcb::send_block send (last_open_hash, key.pub, btcb::Gbcb_ratio, last_keypair.prv, last_keypair.pub, system.work.generate (last_open_hash));
			ASSERT_EQ (btcb::process_result::progress, node->ledger.process (transaction, send).code);
			btcb::open_block open (send.hash (), last_keypair.pub, key.pub, key.prv, key.pub, system.work.generate (key.pub));
			ASSERT_EQ (btcb::process_result::progress, node->ledger.process (transaction, open).code);
			last_open_hash = open.hash ();
			last_keypair = key;
		}
	}

	// Call block confirm on the last open block which will confirm everything
	{
		auto transaction = node->store.tx_begin_read ();
		auto block = node->store.block_get (transaction, last_open_hash);
		node->block_confirm (block);
	}

	system.deadline_set (60s);
	while (true)
	{
		auto transaction = node->store.tx_begin_read ();
		if (node->ledger.block_confirmed (transaction, last_open_hash))
		{
			break;
		}

		ASSERT_NO_ERROR (system.poll ());
	}

	auto transaction (node->store.tx_begin_read ());
	// All frontiers (except last) should have 2 blocks and both should be confirmed
	for (auto i (node->store.latest_begin (transaction)), n (node->store.latest_end ()); i != n; ++i)
	{
		auto & account = i->first;
		auto & account_info = i->second;
		if (account != last_keypair.pub)
		{
			ASSERT_EQ (2, account_info.confirmation_height);
			ASSERT_EQ (2, account_info.block_count);
		}
		else
		{
			ASSERT_EQ (1, account_info.confirmation_height);
			ASSERT_EQ (1, account_info.block_count);
		}
	}

	ASSERT_EQ (node->ledger.stats.count (btcb::stat::type::confirmation_height, btcb::stat::detail::blocks_confirmed, btcb::stat::dir::in), num_accounts * 2 - 2);
}

TEST (confirmation_height, long_chains)
{
	bool delay_frontier_confirmation_height_updating = true;
	btcb::system system;
	auto node = system.add_node (btcb::node_config (24000, system.logging), delay_frontier_confirmation_height_updating);
	btcb::keypair key1;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::block_hash latest (node->latest (btcb::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (key1.prv);

	constexpr auto num_blocks = 10000;

	// First open the other account
	btcb::send_block send (latest, key1.pub, btcb::genesis_amount - btcb::Gbcb_ratio + num_blocks + 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (latest));
	btcb::open_block open (send.hash (), btcb::genesis_account, key1.pub, key1.prv, key1.pub, system.work.generate (key1.pub));
	{
		auto transaction = node->store.tx_begin_write ();
		ASSERT_EQ (btcb::process_result::progress, node->ledger.process (transaction, send).code);
		ASSERT_EQ (btcb::process_result::progress, node->ledger.process (transaction, open).code);
	}

	// Bulk send from genesis account to destination account
	auto previous_genesis_chain_hash = send.hash ();
	auto previous_destination_chain_hash = open.hash ();
	{
		auto transaction = node->store.tx_begin_write ();
		for (auto i = num_blocks - 1; i > 0; --i)
		{
			btcb::send_block send (previous_genesis_chain_hash, key1.pub, btcb::genesis_amount - btcb::Gbcb_ratio + i + 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (previous_genesis_chain_hash));
			ASSERT_EQ (btcb::process_result::progress, node->ledger.process (transaction, send).code);
			btcb::receive_block receive (previous_destination_chain_hash, send.hash (), key1.prv, key1.pub, system.work.generate (previous_destination_chain_hash));
			ASSERT_EQ (btcb::process_result::progress, node->ledger.process (transaction, receive).code);

			previous_genesis_chain_hash = send.hash ();
			previous_destination_chain_hash = receive.hash ();
		}
	}

	// Send one from destination to genesis and pocket it
	btcb::send_block send1 (previous_destination_chain_hash, btcb::test_genesis_key.pub, btcb::Gbcb_ratio - 2, key1.prv, key1.pub, system.work.generate (previous_destination_chain_hash));
	auto receive1 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, previous_genesis_chain_hash, btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio + 1, send1.hash (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (previous_genesis_chain_hash)));

	// Unpocketed
	btcb::state_block send2 (btcb::genesis_account, receive1->hash (), btcb::genesis_account, btcb::genesis_amount - btcb::Gbcb_ratio, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (receive1->hash ()));

	{
		auto transaction = node->store.tx_begin_write ();
		ASSERT_EQ (btcb::process_result::progress, node->ledger.process (transaction, send1).code);
		ASSERT_EQ (btcb::process_result::progress, node->ledger.process (transaction, *receive1).code);
		ASSERT_EQ (btcb::process_result::progress, node->ledger.process (transaction, send2).code);
	}

	// Call block confirm on the existing receive block on the genesis account which will confirm everything underneath on both accounts
	node->block_confirm (receive1);

	system.deadline_set (10s);
	while (true)
	{
		auto transaction = node->store.tx_begin_read ();
		if (node->ledger.block_confirmed (transaction, receive1->hash ()))
		{
			break;
		}

		ASSERT_NO_ERROR (system.poll ());
	}

	auto transaction (node->store.tx_begin_read ());
	btcb::account_info account_info;
	ASSERT_FALSE (node->store.account_get (transaction, btcb::test_genesis_key.pub, account_info));
	ASSERT_EQ (num_blocks + 2, account_info.confirmation_height);
	ASSERT_EQ (num_blocks + 3, account_info.block_count); // Includes the unpocketed send

	ASSERT_FALSE (node->store.account_get (transaction, key1.pub, account_info));
	ASSERT_EQ (num_blocks + 1, account_info.confirmation_height);
	ASSERT_EQ (num_blocks + 1, account_info.block_count);

	ASSERT_EQ (node->ledger.stats.count (btcb::stat::type::confirmation_height, btcb::stat::detail::blocks_confirmed, btcb::stat::dir::in), num_blocks * 2 + 2);
}
