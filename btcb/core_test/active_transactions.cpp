#include <gtest/gtest.h>
#include <btcb/core_test/testutil.hpp>
#include <btcb/lib/jsonconfig.hpp>
#include <btcb/node/testing.hpp>

using namespace std::chrono_literals;

TEST (transaction_counter, validate)
{
	auto now = std::chrono::steady_clock::now ();
	btcb::transaction_counter counter;
	auto count (0);
	ASSERT_EQ (count, counter.get_rate ());
	while (std::chrono::steady_clock::now () < now + 1s)
	{
		count++;
		counter.add ();
	}
	counter.trend_sample ();
	ASSERT_EQ (count, counter.get_rate ());
}

TEST (active_transactions, long_unconfirmed_size)
{
	btcb::system system;
	btcb::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	auto & node1 = *system.add_node (node_config);
	auto & wallet (*system.wallet (0));
	btcb::genesis genesis;
	wallet.insert_adhoc (btcb::test_genesis_key.prv);
	btcb::keypair key1;
	auto send1 (wallet.send_action (btcb::test_genesis_key.pub, btcb::test_genesis_key.pub, btcb::Mbcb_ratio));
	auto send2 (wallet.send_action (btcb::test_genesis_key.pub, btcb::test_genesis_key.pub, btcb::Mbcb_ratio));
	auto send3 (wallet.send_action (btcb::test_genesis_key.pub, btcb::test_genesis_key.pub, btcb::Mbcb_ratio));
	system.deadline_set (10s);
	while (node1.active.size () != 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto done (false);
	while (!done)
	{
		ASSERT_FALSE (node1.active.empty ());
		{
			std::lock_guard<std::mutex> guard (node1.active.mutex);
			done = node1.active.long_unconfirmed_size == 3;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	{
		//since send1 is long_unconfirmed the other two should be as well
		std::lock_guard<std::mutex> lock (node1.active.mutex);
		ASSERT_EQ (node1.active.long_unconfirmed_size, 3);
	}
	{
		std::lock_guard<std::mutex> guard (node1.active.mutex);
		auto existing (node1.active.roots.find (send1->qualified_root ()));
		ASSERT_NE (node1.active.roots.end (), existing);
		//force election to appear confirmed
		auto election (existing->election);
		election->confirm_once ();
	}
	{
		//only 2 should appear unconfirmed now
		std::lock_guard<std::mutex> lock (node1.active.mutex);
		ASSERT_EQ (node1.active.long_unconfirmed_size, 2);
	}
}

TEST (active_transactions, adjusted_difficulty_priority)
{
	btcb::system system;
	btcb::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	auto & node1 = *system.add_node (node_config);
	btcb::genesis genesis;
	btcb::keypair key1, key2, key3;
	auto transaction (node1.store.tx_begin_read ());

	auto send1 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 10 * btcb::bcb_ratio, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send1->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 20 * btcb::bcb_ratio, key2.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (send1->hash ())));
	auto open1 (std::make_shared<btcb::state_block> (key1.pub, 0, key1.pub, 10 * btcb::bcb_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub)));
	auto open2 (std::make_shared<btcb::state_block> (key2.pub, 0, key2.pub, 10 * btcb::bcb_ratio, send2->hash (), key2.prv, key2.pub, system.work.generate (key2.pub)));
	node1.process_active (send1);
	node1.process_active (send2);
	node1.process_active (open1);
	node1.process_active (open2);
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	while (node1.active.size () != 0)
	{
		std::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto it (node1.active.roots.begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.end ())
		{
			auto election (it->election);
			election->confirm_once ();
			it++;
		}
	}

	system.deadline_set (10s);
	while (node1.active.confirmed.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	//genesis and key1,key2 are opened
	//start chain of 2 on each
	auto send3 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send2->hash (), btcb::test_genesis_key.pub, 9 * btcb::bcb_ratio, key3.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (send2->hash (), btcb::difficulty::from_multiplier (1500, node1.network_params.network.publish_threshold))));
	auto send4 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send3->hash (), btcb::test_genesis_key.pub, 8 * btcb::bcb_ratio, key3.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (send3->hash (), btcb::difficulty::from_multiplier (1500, node1.network_params.network.publish_threshold))));
	auto send5 (std::make_shared<btcb::state_block> (key1.pub, open1->hash (), key1.pub, 9 * btcb::bcb_ratio, key3.pub, key1.prv, key1.pub, system.work.generate (open1->hash (), btcb::difficulty::from_multiplier (100, node1.network_params.network.publish_threshold))));
	auto send6 (std::make_shared<btcb::state_block> (key1.pub, send5->hash (), key1.pub, 8 * btcb::bcb_ratio, key3.pub, key1.prv, key1.pub, system.work.generate (send5->hash (), btcb::difficulty::from_multiplier (100, node1.network_params.network.publish_threshold))));
	auto send7 (std::make_shared<btcb::state_block> (key2.pub, open2->hash (), key2.pub, 9 * btcb::bcb_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (open2->hash (), btcb::difficulty::from_multiplier (500, node1.network_params.network.publish_threshold))));
	auto send8 (std::make_shared<btcb::state_block> (key2.pub, send7->hash (), key2.pub, 8 * btcb::bcb_ratio, key3.pub, key2.prv, key2.pub, system.work.generate (send7->hash (), btcb::difficulty::from_multiplier (500, node1.network_params.network.publish_threshold))));

	node1.process_active (send3); //genesis
	node1.process_active (send5); //key1
	node1.process_active (send7); //key2
	node1.process_active (send4); //genesis
	node1.process_active (send6); //key1
	node1.process_active (send8); //key2

	system.deadline_set (10s);
	while (node1.active.size () != 6)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	std::lock_guard<std::mutex> lock (node1.active.mutex);
	uint64_t last_adjusted (0);
	for (auto i (node1.active.roots.get<1> ().begin ()), n (node1.active.roots.get<1> ().end ()); i != n; ++i)
	{
		//first root has nothing to compare
		if (last_adjusted != 0)
		{
			ASSERT_LT (i->adjusted_difficulty, last_adjusted);
		}
		last_adjusted = i->adjusted_difficulty;
	}
}

TEST (active_transactions, keep_local)
{
	//delay_frontier_confirmation_height_updating to allow the test to before
	bool delay_frontier_confirmation_height_updating = true;
	btcb::system system;
	btcb::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	auto & node1 = *system.add_node (node_config, delay_frontier_confirmation_height_updating);
	auto & wallet (*system.wallet (0));
	btcb::genesis genesis;
	//key 1/2 will be managed by the wallet
	btcb::keypair key1, key2, key3, key4;
	wallet.insert_adhoc (btcb::test_genesis_key.prv);
	wallet.insert_adhoc (key1.prv);
	wallet.insert_adhoc (key2.prv);
	auto send1 (wallet.send_action (btcb::test_genesis_key.pub, key1.pub, node1.config.receive_minimum.number ()));
	auto send2 (wallet.send_action (btcb::test_genesis_key.pub, key2.pub, node1.config.receive_minimum.number ()));
	auto send3 (wallet.send_action (btcb::test_genesis_key.pub, key3.pub, node1.config.receive_minimum.number ()));
	auto send4 (wallet.send_action (btcb::test_genesis_key.pub, key4.pub, node1.config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (node1.active.size () != 0)
	{
		std::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto it (node1.active.roots.begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.end ())
		{
			(it->election)->confirm_once ();
			it++;
		}
	}
	auto open1 (std::make_shared<btcb::state_block> (key3.pub, 0, key3.pub, btcb::bcb_ratio, send3->hash (), key3.prv, key3.pub, system.work.generate (key3.pub)));
	node1.process_active (open1);
	auto open2 (std::make_shared<btcb::state_block> (key4.pub, 0, key4.pub, btcb::bcb_ratio, send4->hash (), key4.prv, key4.pub, system.work.generate (key4.pub)));
	node1.process_active (open2);
	//none are dropped since none are long_unconfirmed
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto done (false);
	//wait for all to be long_unconfirmed
	system.deadline_set (10s);
	while (!done)
	{
		ASSERT_FALSE (node1.active.empty ());
		{
			std::lock_guard<std::mutex> guard (node1.active.mutex);
			done = node1.active.long_unconfirmed_size == 4;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	auto send5 (wallet.send_action (btcb::test_genesis_key.pub, key1.pub, node1.config.receive_minimum.number ()));
	node1.active.start (send5);
	//drop two lowest non-wallet managed active_transactions before inserting a new into active as all are long_unconfirmed
	system.deadline_set (10s);
	while (node1.active.size () != 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (active_transactions, prioritize_chains)
{
	//delay_frontier_confirmation_height_updating to allow the test to before
	bool delay_frontier_confirmation_height_updating = true;
	btcb::system system;
	btcb::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	auto & node1 = *system.add_node (node_config, delay_frontier_confirmation_height_updating);
	btcb::genesis genesis;
	btcb::keypair key1, key2, key3;

	auto send1 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 10 * btcb::bcb_ratio, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto open1 (std::make_shared<btcb::state_block> (key1.pub, 0, key1.pub, 10 * btcb::bcb_ratio, send1->hash (), key1.prv, key1.pub, system.work.generate (key1.pub)));
	auto send2 (std::make_shared<btcb::state_block> (key1.pub, open1->hash (), key1.pub, btcb::bcb_ratio * 9, key2.pub, key1.prv, key1.pub, system.work.generate (open1->hash ())));
	auto send3 (std::make_shared<btcb::state_block> (key1.pub, send2->hash (), key1.pub, btcb::bcb_ratio * 8, key2.pub, key1.prv, key1.pub, system.work.generate (send2->hash ())));
	auto send4 (std::make_shared<btcb::state_block> (key1.pub, send3->hash (), key1.pub, btcb::bcb_ratio * 7, key2.pub, key1.prv, key1.pub, system.work.generate (send3->hash ())));
	auto send5 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send1->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 20 * btcb::bcb_ratio, key2.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (send1->hash ())));
	auto send6 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send5->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 30 * btcb::bcb_ratio, key3.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (send5->hash ())));
	auto open2 (std::make_shared<btcb::state_block> (key2.pub, 0, key2.pub, 10 * btcb::bcb_ratio, send5->hash (), key2.prv, key2.pub, system.work.generate (key2.pub, btcb::difficulty::from_multiplier (50., node1.network_params.network.publish_threshold))));
	uint64_t difficulty1 (0);
	btcb::work_validate (*open2, &difficulty1);
	uint64_t difficulty2 (0);
	btcb::work_validate (*send6, &difficulty2);

	node1.process_active (send1);
	node1.process_active (open1);
	node1.process_active (send5);
	system.deadline_set (10s);
	while (node1.active.size () != 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (node1.active.size () != 0)
	{
		std::lock_guard<std::mutex> active_guard (node1.active.mutex);
		auto it (node1.active.roots.get<1> ().begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.get<1> ().end ())
		{
			auto election (it->election);
			election->confirm_once ();
			it++;
		}
	}

	node1.process_active (send2);
	node1.process_active (send3);
	node1.process_active (send4);
	node1.process_active (send6);

	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	bool done (false);
	//wait for all to be long_unconfirmed
	while (!done)
	{
		{
			std::lock_guard<std::mutex> guard (node1.active.mutex);
			done = node1.active.long_unconfirmed_size == 4;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	std::this_thread::sleep_for (1s);
	node1.process_active (open2);
	system.deadline_set (10s);
	while (node1.active.size () != 4)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	//wait for all to be long_unconfirmed
	done = false;
	system.deadline_set (10s);
	while (!done)
	{
		{
			std::lock_guard<std::mutex> guard (node1.active.mutex);
			done = node1.active.long_unconfirmed_size == 4;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	size_t seen (0);
	{
		auto it (node1.active.roots.get<1> ().begin ());
		while (!node1.active.roots.empty () && it != node1.active.roots.get<1> ().end ())
		{
			if (it->difficulty == (difficulty1 || difficulty2))
			{
				seen++;
			}
			it++;
		}
	}
	ASSERT_LT (seen, 2);
	ASSERT_EQ (node1.active.size (), 4);
}
