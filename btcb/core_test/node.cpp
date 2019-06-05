#include <gtest/gtest.h>
#include <btcb/core_test/testutil.hpp>
#include <btcb/lib/jsonconfig.hpp>
#include <btcb/node/testing.hpp>
#include <btcb/node/transport/udp.hpp>
#include <btcb/node/working.hpp>
#include <numeric>

#include <boost/make_shared.hpp>
#include <boost/polymorphic_cast.hpp>

using namespace std::chrono_literals;

namespace
{
void add_required_children_node_config_tree (btcb::jsonconfig & tree);
}

TEST (node, stop)
{
	btcb::system system (24000, 1);
	ASSERT_NE (system.nodes[0]->wallets.items.end (), system.nodes[0]->wallets.items.begin ());
	system.nodes[0]->stop ();
	system.io_ctx.run ();
	ASSERT_TRUE (true);
}

TEST (node, block_store_path_failure)
{
	btcb::node_init init;
	auto service (boost::make_shared<boost::asio::io_context> ());
	btcb::alarm alarm (*service);
	auto path (btcb::unique_path ());
	btcb::logging logging;
	logging.init (path);
	btcb::work_pool work (std::numeric_limits<unsigned>::max ());
	auto node (std::make_shared<btcb::node> (init, *service, 24000, path, alarm, logging, work));
	ASSERT_TRUE (node->wallets.items.empty ());
	node->stop ();
}

TEST (node, password_fanout)
{
	btcb::node_init init;
	auto service (boost::make_shared<boost::asio::io_context> ());
	btcb::alarm alarm (*service);
	auto path (btcb::unique_path ());
	btcb::node_config config;
	config.peering_port = 24000;
	config.logging.init (path);
	btcb::work_pool work (std::numeric_limits<unsigned>::max ());
	config.password_fanout = 10;
	auto node (std::make_shared<btcb::node> (init, *service, path, alarm, config, work));
	auto wallet (node->wallets.create (100));
	ASSERT_EQ (10, wallet->store.password.values.size ());
	node->stop ();
}

TEST (node, balance)
{
	btcb::system system (24000, 1);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	auto transaction (system.nodes[0]->store.tx_begin_write ());
	ASSERT_EQ (std::numeric_limits<btcb::uint128_t>::max (), system.nodes[0]->ledger.account_balance (transaction, btcb::test_genesis_key.pub));
}

TEST (node, representative)
{
	btcb::system system (24000, 1);
	auto block1 (system.nodes[0]->representative (btcb::test_genesis_key.pub));
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_TRUE (system.nodes[0]->ledger.store.block_exists (transaction, block1));
	}
	btcb::keypair key;
	ASSERT_TRUE (system.nodes[0]->representative (key.pub).is_zero ());
}

TEST (node, send_unkeyed)
{
	btcb::system system (24000, 1);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	system.wallet (0)->store.password.value_set (btcb::keypair ().prv);
	ASSERT_EQ (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
}

TEST (node, send_self)
{
	btcb::system system (24000, 1);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (btcb::test_genesis_key.pub));
}

TEST (node, send_single)
{
	btcb::system system (24000, 2);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (btcb::test_genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, send_single_observing_peer)
{
	btcb::system system (24000, 3);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (btcb::test_genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	system.deadline_set (10s);
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<btcb::node> const & node_a) { return node_a->balance (key2.pub).is_zero (); }))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, send_single_many_peers)
{
	btcb::system system (24000, 10);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	system.wallet (1)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_EQ (std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (btcb::test_genesis_key.pub));
	ASSERT_TRUE (system.nodes[0]->balance (key2.pub).is_zero ());
	system.deadline_set (3.5min);
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<btcb::node> const & node_a) { return node_a->balance (key2.pub).is_zero (); }))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, send_out_of_order)
{
	btcb::system system (24000, 2);
	btcb::keypair key2;
	btcb::genesis genesis;
	btcb::send_block send1 (genesis.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ()));
	btcb::send_block send2 (send1.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 2, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (send1.hash ()));
	btcb::send_block send3 (send2.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 3, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (send2.hash ()));
	system.nodes[0]->process_active (std::make_shared<btcb::send_block> (send3));
	system.nodes[0]->process_active (std::make_shared<btcb::send_block> (send2));
	system.nodes[0]->process_active (std::make_shared<btcb::send_block> (send1));
	system.deadline_set (10s);
	while (std::any_of (system.nodes.begin (), system.nodes.end (), [&](std::shared_ptr<btcb::node> const & node_a) { return node_a->balance (btcb::test_genesis_key.pub) != btcb::genesis_amount - system.nodes[0]->config.receive_minimum.number () * 3; }))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, quick_confirm)
{
	btcb::system system (24000, 1);
	btcb::keypair key;
	btcb::block_hash previous (system.nodes[0]->latest (btcb::test_genesis_key.pub));
	auto genesis_start_balance (system.nodes[0]->balance (btcb::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	auto send (std::make_shared<btcb::send_block> (previous, key.pub, system.nodes[0]->config.online_weight_minimum.number () + 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (previous)));
	system.nodes[0]->process_active (send);
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (system.nodes[0]->balance (btcb::test_genesis_key.pub), system.nodes[0]->config.online_weight_minimum.number () + 1);
	ASSERT_EQ (system.nodes[0]->balance (key.pub), genesis_start_balance - (system.nodes[0]->config.online_weight_minimum.number () + 1));
}

TEST (node, node_receive_quorum)
{
	btcb::system system (24000, 1);
	btcb::keypair key;
	btcb::block_hash previous (system.nodes[0]->latest (btcb::test_genesis_key.pub));
	system.wallet (0)->insert_adhoc (key.prv);
	auto send (std::make_shared<btcb::send_block> (previous, key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (previous)));
	system.nodes[0]->process_active (send);
	system.deadline_set (10s);
	while (!system.nodes[0]->ledger.block_exists (send->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto done (false);
	while (!done)
	{
		{
			std::lock_guard<std::mutex> guard (system.nodes[0]->active.mutex);
			auto info (system.nodes[0]->active.roots.find (btcb::qualified_root (previous, previous)));
			ASSERT_NE (system.nodes[0]->active.roots.end (), info);
			done = info->election->announcements > btcb::active_transactions::announcement_min;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	btcb::system system2 (24001, 1);
	system2.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	ASSERT_TRUE (system.nodes[0]->balance (key.pub).is_zero ());
	auto channel (std::make_shared<btcb::transport::channel_udp> (system.nodes[0]->network.udp_channels, system2.nodes[0]->network.endpoint ()));
	system.nodes[0]->network.send_keepalive (channel);
	while (system.nodes[0]->balance (key.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
		ASSERT_NO_ERROR (system2.poll ());
	}
}

TEST (node, auto_bootstrap)
{
	btcb::system system (24000, 1);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	btcb::node_init init1;
	auto node1 (std::make_shared<btcb::node> (init1, system.io_ctx, 24001, btcb::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	auto channel (std::make_shared<btcb::transport::channel_udp> (node1->network.udp_channels, system.nodes[0]->network.endpoint ()));
	node1->network.send_keepalive (channel);
	node1->start ();
	system.nodes.push_back (node1);
	while (!node1->bootstrap_initiator.in_progress ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	while (node1->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.deadline_set (10s);
	while (node1->bootstrap_initiator.in_progress ())
	{
		ASSERT_NO_ERROR (system.poll ());
	};
	node1->stop ();
}

TEST (node, auto_bootstrap_reverse)
{
	btcb::system system (24000, 1);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	btcb::node_init init1;
	auto node1 (std::make_shared<btcb::node> (init1, system.io_ctx, 24001, btcb::unique_path (), system.alarm, system.logging, system.work));
	ASSERT_FALSE (init1.error ());
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	auto channel (std::make_shared<btcb::transport::channel_udp> (system.nodes[0]->network.udp_channels, node1->network.endpoint ()));
	system.nodes[0]->network.send_keepalive (channel);
	node1->start ();
	system.nodes.push_back (node1);
	system.deadline_set (10s);
	while (node1->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (node, receive_gap)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	ASSERT_EQ (0, node1.gap_cache.size ());
	auto block (std::make_shared<btcb::send_block> (5, 1, 2, btcb::keypair ().prv, 4, 0));
	node1.work_generate_blocking (*block);
	btcb::publish message (block);
	node1.process_message (message, node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.block_processor.flush ();
	ASSERT_EQ (1, node1.gap_cache.size ());
}

TEST (node, merge_peers)
{
	btcb::system system (24000, 1);
	std::array<btcb::endpoint, 8> endpoints;
	endpoints.fill (btcb::endpoint (boost::asio::ip::address_v6::loopback (), 24000));
	endpoints[0] = btcb::endpoint (boost::asio::ip::address_v6::loopback (), 24001);
	system.nodes[0]->network.merge_peers (endpoints);
	ASSERT_EQ (0, system.nodes[0]->network.size ());
}

TEST (node, search_pending)
{
	btcb::system system (24000, 1);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node (system.nodes[0]);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, search_pending_same)
{
	btcb::system system (24000, 1);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node (system.nodes[0]);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub) != 2 * system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, search_pending_multiple)
{
	btcb::system system (24000, 1);
	btcb::keypair key2;
	btcb::keypair key3;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key3.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key3.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key3.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, system.wallet (0)->send_action (key3.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.wallet (0)->insert_adhoc (key2.prv);
	auto node (system.nodes[0]);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub) != 2 * system.nodes[0]->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, search_pending_confirmed)
{
	btcb::system system (24000, 1);
	auto node (system.nodes[0]);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	auto send1 (system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send1);
	auto send2 (system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, node->config.receive_minimum.number ()));
	ASSERT_NE (nullptr, send2);
	system.deadline_set (10s);
	while (!node->active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	{
		auto transaction (node->store.tx_begin_read ());
		ASSERT_TRUE (node->ledger.block_confirmed (transaction, send2->hash ()));
	}
	{
		auto transaction (node->wallets.tx_begin_write ());
		system.wallet (0)->store.erase (transaction, btcb::test_genesis_key.pub);
	}
	system.wallet (0)->insert_adhoc (key2.prv);
	ASSERT_FALSE (system.wallet (0)->search_pending ());
	{
		std::lock_guard<std::mutex> guard (node->active.mutex);
		auto existing1 (node->active.blocks.find (send1->hash ()));
		ASSERT_EQ (node->active.blocks.end (), existing1);
		auto existing2 (node->active.blocks.find (send2->hash ()));
		ASSERT_EQ (node->active.blocks.end (), existing2);
	}
	system.deadline_set (10s);
	while (node->balance (key2.pub) != 2 * node->config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, unlock_search)
{
	btcb::system system (24000, 1);
	btcb::keypair key2;
	btcb::uint128_t balance (system.nodes[0]->balance (btcb::test_genesis_key.pub));
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		system.wallet (0)->store.rekey (transaction, "");
	}
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (btcb::test_genesis_key.pub) == balance)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!system.nodes[0]->active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.wallet (0)->insert_adhoc (key2.prv);
	{
		std::lock_guard<std::recursive_mutex> lock (system.wallet (0)->store.mutex);
		system.wallet (0)->store.password.value_set (btcb::keypair ().prv);
	}
	auto node (system.nodes[0]);
	{
		auto transaction (system.wallet (0)->wallets.tx_begin_write ());
		ASSERT_FALSE (system.wallet (0)->enter_password (transaction, ""));
	}
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, connect_after_junk)
{
	btcb::system system (24000, 1);
	btcb::node_init init1;
	auto node1 (std::make_shared<btcb::node> (init1, system.io_ctx, 24001, btcb::unique_path (), system.alarm, system.logging, system.work));
	auto junk_buffer (std::make_shared<std::vector<uint8_t>> ());
	junk_buffer->push_back (0);
	auto channel1 (std::make_shared<btcb::transport::channel_udp> (node1->network.udp_channels, system.nodes[0]->network.endpoint ()));
	channel1->send_buffer (junk_buffer, btcb::stat::detail::bulk_pull, [](boost::system::error_code const &, size_t) {});
	system.deadline_set (10s);
	while (system.nodes[0]->stats.count (btcb::stat::type::error) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->start ();
	system.nodes.push_back (node1);
	auto channel2 (std::make_shared<btcb::transport::channel_udp> (node1->network.udp_channels, system.nodes[0]->network.endpoint ()));
	node1->network.send_keepalive (channel2);
	system.deadline_set (10s);
	while (node1->network.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node1->stop ();
}

TEST (node, working)
{
	auto path (btcb::working_path ());
	ASSERT_FALSE (path.empty ());
}

TEST (node, price)
{
	btcb::system system (24000, 1);
	auto price1 (system.nodes[0]->price (btcb::Gbcb_ratio, 1));
	ASSERT_EQ (btcb::node::price_max * 100.0, price1);
	auto price2 (system.nodes[0]->price (btcb::Gbcb_ratio * int(btcb::node::free_cutoff + 1), 1));
	ASSERT_EQ (0, price2);
	auto price3 (system.nodes[0]->price (btcb::Gbcb_ratio * int(btcb::node::free_cutoff + 2) / 2, 1));
	ASSERT_EQ (btcb::node::price_max * 100.0 / 2, price3);
	auto price4 (system.nodes[0]->price (btcb::Gbcb_ratio * int(btcb::node::free_cutoff) * 2, 1));
	ASSERT_EQ (0, price4);
}

TEST (node, confirm_locked)
{
	btcb::system system (24000, 1);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	auto transaction (system.nodes[0]->store.tx_begin_read ());
	system.wallet (0)->enter_password (transaction, "1");
	auto block (std::make_shared<btcb::send_block> (0, 0, 0, btcb::keypair ().prv, 0, 0));
	system.nodes[0]->network.flood_block (block);
}

TEST (node_config, serialization)
{
	auto path (btcb::unique_path ());
	btcb::logging logging1;
	logging1.init (path);
	btcb::node_config config1 (100, logging1);
	config1.bootstrap_fraction_numerator = 10;
	config1.receive_minimum = 10;
	config1.online_weight_minimum = 10;
	config1.online_weight_quorum = 10;
	config1.password_fanout = 20;
	config1.enable_voting = false;
	config1.callback_address = "test";
	config1.callback_port = 10;
	config1.callback_target = "test";
	config1.lmdb_max_dbs = 256;
	btcb::jsonconfig tree;
	config1.serialize_json (tree);
	btcb::logging logging2;
	logging2.init (path);
	logging2.node_lifetime_tracing_value = !logging2.node_lifetime_tracing_value;
	btcb::node_config config2 (50, logging2);
	ASSERT_NE (config2.bootstrap_fraction_numerator, config1.bootstrap_fraction_numerator);
	ASSERT_NE (config2.peering_port, config1.peering_port);
	ASSERT_NE (config2.logging.node_lifetime_tracing_value, config1.logging.node_lifetime_tracing_value);
	ASSERT_NE (config2.online_weight_minimum, config1.online_weight_minimum);
	ASSERT_NE (config2.online_weight_quorum, config1.online_weight_quorum);
	ASSERT_NE (config2.password_fanout, config1.password_fanout);
	ASSERT_NE (config2.enable_voting, config1.enable_voting);
	ASSERT_NE (config2.callback_address, config1.callback_address);
	ASSERT_NE (config2.callback_port, config1.callback_port);
	ASSERT_NE (config2.callback_target, config1.callback_target);
	ASSERT_NE (config2.lmdb_max_dbs, config1.lmdb_max_dbs);

	ASSERT_FALSE (tree.get_optional<std::string> ("epoch_block_link"));
	ASSERT_FALSE (tree.get_optional<std::string> ("epoch_block_signer"));

	bool upgraded (false);
	ASSERT_FALSE (config2.deserialize_json (upgraded, tree));
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (config2.bootstrap_fraction_numerator, config1.bootstrap_fraction_numerator);
	ASSERT_EQ (config2.peering_port, config1.peering_port);
	ASSERT_EQ (config2.logging.node_lifetime_tracing_value, config1.logging.node_lifetime_tracing_value);
	ASSERT_EQ (config2.online_weight_minimum, config1.online_weight_minimum);
	ASSERT_EQ (config2.online_weight_quorum, config1.online_weight_quorum);
	ASSERT_EQ (config2.password_fanout, config1.password_fanout);
	ASSERT_EQ (config2.enable_voting, config1.enable_voting);
	ASSERT_EQ (config2.callback_address, config1.callback_address);
	ASSERT_EQ (config2.callback_port, config1.callback_port);
	ASSERT_EQ (config2.callback_target, config1.callback_target);
	ASSERT_EQ (config2.lmdb_max_dbs, config1.lmdb_max_dbs);
}

TEST (node_config, v1_v2_upgrade)
{
	auto path (btcb::unique_path ());
	btcb::logging logging1;
	logging1.init (path);
	btcb::jsonconfig tree;
	tree.put ("peering_port", std::to_string (0));
	tree.put ("packet_delay_microseconds", std::to_string (0));
	tree.put ("bootstrap_fraction_numerator", std::to_string (0));
	tree.put ("creation_rebroadcast", std::to_string (0));
	tree.put ("rebroadcast_delay", std::to_string (0));
	tree.put ("receive_minimum", btcb::amount (0).to_string_dec ());
	btcb::jsonconfig logging_l;
	logging1.serialize_json (logging_l);
	tree.put_child ("logging", logging_l);
	btcb::jsonconfig preconfigured_peers_l;
	tree.put_child ("preconfigured_peers", preconfigured_peers_l);
	btcb::jsonconfig preconfigured_representatives_l;
	tree.put_child ("preconfigured_representatives", preconfigured_representatives_l);
	bool upgraded (false);
	btcb::node_config config1;
	config1.logging.init (path);
	ASSERT_FALSE (tree.get_optional_child ("work_peers"));
	config1.deserialize_json (upgraded, tree);
	ASSERT_TRUE (upgraded);
	ASSERT_TRUE (!!tree.get_optional_child ("work_peers"));
}

TEST (node_config, v2_v3_upgrade)
{
	btcb::jsonconfig tree;
	add_required_children_node_config_tree (tree);
	tree.put ("peering_port", std::to_string (0));
	tree.put ("packet_delay_microseconds", std::to_string (0));
	tree.put ("bootstrap_fraction_numerator", std::to_string (0));
	tree.put ("creation_rebroadcast", std::to_string (0));
	tree.put ("rebroadcast_delay", std::to_string (0));
	tree.put ("receive_minimum", btcb::amount (0).to_string_dec ());
	tree.put ("version", "2");

	btcb::jsonconfig preconfigured_representatives_l;
	preconfigured_representatives_l.push ("TR6ZJ4pdp6HC76xMRpVDny5x2s8AEbrhFue3NKVxYYdmKuTEib");
	tree.replace_child ("preconfigured_representatives", preconfigured_representatives_l);

	bool upgraded (false);
	btcb::node_config config1;
	auto path (btcb::unique_path ());
	config1.logging.init (path);
	ASSERT_FALSE (tree.get_optional<std::string> ("inactive_supply"));
	ASSERT_FALSE (tree.get_optional<std::string> ("password_fanout"));
	ASSERT_FALSE (tree.get_optional<std::string> ("io_threads"));
	ASSERT_FALSE (tree.get_optional<std::string> ("work_threads"));
	config1.deserialize_json (upgraded, tree);
	//ASSERT_EQ (btcb::uint128_union (0).to_string_dec (), tree.get<std::string> ("inactive_supply"));
	ASSERT_EQ ("1024", tree.get<std::string> ("password_fanout"));
	ASSERT_NE (0, std::stoul (tree.get<std::string> ("password_fanout")));
	ASSERT_TRUE (upgraded);
	auto version (tree.get<std::string> ("version"));
	ASSERT_GT (std::stoull (version), 2);
}

TEST (node_config, v15_v16_upgrade)
{
	auto test_upgrade = [](auto old_preconfigured_peers_url, auto new_preconfigured_peers_url) {
		auto path (btcb::unique_path ());
		btcb::jsonconfig tree;
		add_required_children_node_config_tree (tree);
		tree.put ("version", "15");

		const char * dummy_peer = "127.5.2.1";
		btcb::jsonconfig preconfigured_peers_json;
		preconfigured_peers_json.push (old_preconfigured_peers_url);
		preconfigured_peers_json.push (dummy_peer);
		tree.replace_child ("preconfigured_peers", preconfigured_peers_json);

		auto upgraded (false);
		btcb::node_config config;
		config.logging.init (path);
		// These config options should not be present at version 15
		ASSERT_FALSE (tree.get_optional_child ("allow_local_peers"));
		ASSERT_FALSE (tree.get_optional_child ("signature_checker_threads"));
		ASSERT_FALSE (tree.get_optional_child ("vote_minimum"));
		config.deserialize_json (upgraded, tree);
		// The config options should be added after the upgrade
		ASSERT_TRUE (!!tree.get_optional_child ("allow_local_peers"));
		ASSERT_TRUE (!!tree.get_optional_child ("signature_checker_threads"));
		ASSERT_TRUE (!!tree.get_optional_child ("vote_minimum"));

		ASSERT_TRUE (upgraded);
		auto version (tree.get<std::string> ("version"));

		auto read_preconfigured_peers_json (tree.get_required_child ("preconfigured_peers"));
		std::vector<std::string> preconfigured_peers;
		read_preconfigured_peers_json.array_entries<std::string> ([&preconfigured_peers](const auto & entry) {
			preconfigured_peers.push_back (entry);
		});

		// Check that the new peer is updated while the other peer is untouched
		ASSERT_EQ (preconfigured_peers.size (), 2);
		ASSERT_EQ (preconfigured_peers.front (), new_preconfigured_peers_url);
		ASSERT_EQ (preconfigured_peers.back (), dummy_peer);

		// Check version is updated
		ASSERT_GT (std::stoull (version), 15);
	};

	// Check that upgrades work with both
	test_upgrade ("rai.raiblocks.net", "peering.nano.org");
	test_upgrade ("rai-beta.raiblocks.net", "peering-beta.nano.org");
}

TEST (node_config, v16_values)
{
	btcb::jsonconfig tree;
	add_required_children_node_config_tree (tree);

	auto path (btcb::unique_path ());
	auto upgraded (false);
	btcb::node_config config;
	config.logging.init (path);

	// Check config is correct
	tree.put ("allow_local_peers", false);
	tree.put ("signature_checker_threads", 1);
	tree.put ("vote_minimum", btcb::Gbcb_ratio.convert_to<std::string> ());
	config.deserialize_json (upgraded, tree);
	ASSERT_FALSE (upgraded);
	ASSERT_FALSE (config.allow_local_peers);
	ASSERT_EQ (config.signature_checker_threads, 1);
	ASSERT_EQ (config.vote_minimum.number (), btcb::Gbcb_ratio);

	// Check config is correct with other values
	tree.put ("allow_local_peers", true);
	tree.put ("signature_checker_threads", 4);
	tree.put ("vote_minimum", (std::numeric_limits<btcb::uint128_t>::max () - 100).convert_to<std::string> ());
	upgraded = false;
	config.deserialize_json (upgraded, tree);
	ASSERT_FALSE (upgraded);
	ASSERT_TRUE (config.allow_local_peers);
	ASSERT_EQ (config.signature_checker_threads, 4);
	ASSERT_EQ (config.vote_minimum.number (), std::numeric_limits<btcb::uint128_t>::max () - 100);
}

TEST (node_config, v16_v17_upgrade)
{
	auto path (btcb::unique_path ());
	btcb::jsonconfig tree;
	add_required_children_node_config_tree (tree);
	tree.put ("version", "16");

	auto upgraded (false);
	btcb::node_config config;
	config.logging.init (path);
	// These config options should not be present
	ASSERT_FALSE (tree.get_optional_child ("tcp_io_timeout"));
	ASSERT_FALSE (tree.get_optional_child ("tcp_idle_timeout"));
	ASSERT_FALSE (tree.get_optional_child ("pow_sleep_interval"));
	ASSERT_FALSE (tree.get_optional_child ("external_address"));
	ASSERT_FALSE (tree.get_optional_child ("external_port"));
	ASSERT_FALSE (tree.get_optional_child ("tcp_incoming_connections_max"));
	ASSERT_FALSE (tree.get_optional_child ("diagnostics"));

	config.deserialize_json (upgraded, tree);
	// The config options should be added after the upgrade
	ASSERT_TRUE (!!tree.get_optional_child ("tcp_io_timeout"));
	ASSERT_TRUE (!!tree.get_optional_child ("tcp_idle_timeout"));
	ASSERT_TRUE (!!tree.get_optional_child ("pow_sleep_interval"));
	ASSERT_TRUE (!!tree.get_optional_child ("external_address"));
	ASSERT_TRUE (!!tree.get_optional_child ("external_port"));
	ASSERT_TRUE (!!tree.get_optional_child ("tcp_incoming_connections_max"));
	ASSERT_TRUE (!!tree.get_optional_child ("diagnostics"));

	ASSERT_TRUE (upgraded);
	auto version (tree.get<std::string> ("version"));

	// Check version is updated
	ASSERT_GT (std::stoull (version), 16);
}

TEST (node_config, v17_values)
{
	btcb::jsonconfig tree;
	add_required_children_node_config_tree (tree);

	auto path (btcb::unique_path ());
	auto upgraded (false);
	btcb::node_config config;
	config.logging.init (path);

	// Check config is correct
	{
		tree.put ("tcp_io_timeout", 1);
		tree.put ("tcp_idle_timeout", 0);
		tree.put ("pow_sleep_interval", 0);
		tree.put ("external_address", "::1");
		tree.put ("external_port", 0);
		tree.put ("tcp_incoming_connections_max", 1);
		btcb::jsonconfig txn_tracking_l;
		txn_tracking_l.put ("enable", false);
		txn_tracking_l.put ("min_read_txn_time", 0);
		txn_tracking_l.put ("min_write_txn_time", 0);
		txn_tracking_l.put ("ignore_writes_below_block_processor_max_time", true);
		btcb::jsonconfig diagnostics_l;
		diagnostics_l.put_child ("txn_tracking", txn_tracking_l);
		tree.put_child ("diagnostics", diagnostics_l);
	}

	config.deserialize_json (upgraded, tree);
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (config.tcp_io_timeout.count (), 1);
	ASSERT_EQ (config.tcp_idle_timeout.count (), 0);
	ASSERT_EQ (config.pow_sleep_interval.count (), 0);
	ASSERT_EQ (config.external_address, boost::asio::ip::address_v6::from_string ("::1"));
	ASSERT_EQ (config.external_port, 0);
	ASSERT_EQ (config.tcp_incoming_connections_max, 1);
	ASSERT_FALSE (config.diagnostics_config.txn_tracking.enable);
	ASSERT_EQ (config.diagnostics_config.txn_tracking.min_read_txn_time.count (), 0);
	ASSERT_EQ (config.diagnostics_config.txn_tracking.min_write_txn_time.count (), 0);
	ASSERT_TRUE (config.diagnostics_config.txn_tracking.ignore_writes_below_block_processor_max_time);

	// Check config is correct with other values
	tree.put ("tcp_io_timeout", std::numeric_limits<unsigned long>::max () - 100);
	tree.put ("tcp_idle_timeout", std::numeric_limits<unsigned>::max ());
	tree.put ("pow_sleep_interval", std::numeric_limits<unsigned long>::max () - 100);
	tree.put ("external_address", "::ffff:192.168.1.1");
	tree.put ("external_port", std::numeric_limits<uint16_t>::max () - 1);
	tree.put ("tcp_incoming_connections_max", std::numeric_limits<unsigned>::max ());
	btcb::jsonconfig txn_tracking_l;
	txn_tracking_l.put ("enable", true);
	txn_tracking_l.put ("min_read_txn_time", 1234);
	txn_tracking_l.put ("min_write_txn_time", std::numeric_limits<unsigned>::max ());
	txn_tracking_l.put ("ignore_writes_below_block_processor_max_time", false);
	btcb::jsonconfig diagnostics_l;
	diagnostics_l.replace_child ("txn_tracking", txn_tracking_l);
	tree.replace_child ("diagnostics", diagnostics_l);

	upgraded = false;
	config.deserialize_json (upgraded, tree);
	ASSERT_FALSE (upgraded);
	ASSERT_EQ (config.tcp_io_timeout.count (), std::numeric_limits<unsigned long>::max () - 100);
	ASSERT_EQ (config.tcp_idle_timeout.count (), std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (config.pow_sleep_interval.count (), std::numeric_limits<unsigned long>::max () - 100);
	ASSERT_EQ (config.external_address, boost::asio::ip::address_v6::from_string ("::ffff:192.168.1.1"));
	ASSERT_EQ (config.external_port, std::numeric_limits<uint16_t>::max () - 1);
	ASSERT_EQ (config.tcp_incoming_connections_max, std::numeric_limits<unsigned>::max ());
	ASSERT_TRUE (config.diagnostics_config.txn_tracking.enable);
	ASSERT_EQ (config.diagnostics_config.txn_tracking.min_read_txn_time.count (), 1234);
	ASSERT_EQ (config.tcp_incoming_connections_max, std::numeric_limits<unsigned>::max ());
	ASSERT_EQ (config.diagnostics_config.txn_tracking.min_write_txn_time.count (), std::numeric_limits<unsigned>::max ());
	ASSERT_FALSE (config.diagnostics_config.txn_tracking.ignore_writes_below_block_processor_max_time);
}

// Regression test to ensure that deserializing includes changes node via get_required_child
TEST (node_config, required_child)
{
	auto path (btcb::unique_path ());
	btcb::logging logging1;
	btcb::logging logging2;
	logging1.init (path);
	btcb::jsonconfig tree;

	btcb::jsonconfig logging_l;
	logging1.serialize_json (logging_l);
	tree.put_child ("logging", logging_l);
	auto child_l (tree.get_required_child ("logging"));
	child_l.put<bool> ("flush", !logging1.flush);
	bool upgraded (false);
	logging2.deserialize_json (upgraded, child_l);

	ASSERT_NE (logging1.flush, logging2.flush);
}

TEST (node_config, random_rep)
{
	auto path (btcb::unique_path ());
	btcb::logging logging1;
	logging1.init (path);
	btcb::node_config config1 (100, logging1);
	auto rep (config1.random_representative ());
	ASSERT_NE (config1.preconfigured_representatives.end (), std::find (config1.preconfigured_representatives.begin (), config1.preconfigured_representatives.end (), rep));
}

class json_initial_value_test
{
public:
	json_initial_value_test (std::string text_a) :
	text (std::move (text_a))
	{
	}
	btcb::error serialize_json (btcb::jsonconfig & json)
	{
		json.put ("thing", text);
		return json.get_error ();
	}
	std::string text;
};

class json_upgrade_test
{
public:
	btcb::error deserialize_json (bool & upgraded, btcb::jsonconfig & json)
	{
		if (!json.empty ())
		{
			auto text_l (json.get<std::string> ("thing"));
			if (text_l == "junktest" || text_l == "created")
			{
				upgraded = true;
				text_l = "changed";
				json.put ("thing", text_l);
			}
			if (text_l == "error")
			{
				json.get_error () = btcb::error_common::generic;
			}
			text = text_l;
		}
		else
		{
			upgraded = true;
			text = "created";
			json.put ("thing", text);
		}
		return json.get_error ();
	}
	std::string text;
};

/** Both create and upgrade via read_and_update() */
TEST (json, create_and_upgrade)
{
	auto path (btcb::unique_path ());
	btcb::jsonconfig json;
	json_upgrade_test object1;
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ ("created", object1.text);

	btcb::jsonconfig json2;
	json_upgrade_test object2;
	ASSERT_FALSE (json2.read_and_update (object2, path));
	ASSERT_EQ ("changed", object2.text);
}

/** Create config manually, then upgrade via read_and_update() with multiple calls to test idempotence */
TEST (json, upgrade_from_existing)
{
	auto path (btcb::unique_path ());
	btcb::jsonconfig json;
	json_initial_value_test junktest ("junktest");
	junktest.serialize_json (json);
	json.write (path);
	json_upgrade_test object1;
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ ("changed", object1.text);
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ ("changed", object1.text);
}

/** Test that backups are made only when there is an upgrade */
TEST (json, backup)
{
	auto dir (btcb::unique_path ());
	namespace fs = boost::filesystem;
	fs::create_directory (dir);
	auto path = dir / dir.leaf ();

	// Create json file
	btcb::jsonconfig json;
	json_upgrade_test object1;
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ ("created", object1.text);

	/** Returns 'dir' if backup file cannot be found */
	// clang-format off
	auto get_backup_path = [&dir]() {
		for (fs::directory_iterator itr (dir); itr != fs::directory_iterator (); ++itr)
		{
			if (itr->path ().filename ().string ().find ("_backup_") != std::string::npos)
			{
				return itr->path ();
			}
		}
		return dir;
	};

	auto get_file_count = [&dir]() {
		return std::count_if (boost::filesystem::directory_iterator (dir), boost::filesystem::directory_iterator (), static_cast<bool (*) (const boost::filesystem::path &)> (boost::filesystem::is_regular_file));
	};
	// clang-format on

	// There should only be the original file in this directory
	ASSERT_EQ (get_file_count (), 1);
	ASSERT_EQ (get_backup_path (), dir);

	// Upgrade, check that there is a backup which matches the first object
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ (get_file_count (), 2);
	ASSERT_NE (get_backup_path (), path);

	// Check there is a backup which has the same contents as the original file
	btcb::jsonconfig json1;
	ASSERT_FALSE (json1.read (get_backup_path ()));
	ASSERT_EQ (json1.get<std::string> ("thing"), "created");

	// Try and upgrade an already upgraded file, should not create any backups
	ASSERT_FALSE (json.read_and_update (object1, path));
	ASSERT_EQ (get_file_count (), 2);
}

TEST (node, fork_publish)
{
	std::weak_ptr<btcb::node> node0;
	{
		btcb::system system (24000, 1);
		node0 = system.nodes[0];
		auto & node1 (*system.nodes[0]);
		system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
		btcb::keypair key1;
		btcb::genesis genesis;
		auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
		node1.work_generate_blocking (*send1);
		btcb::keypair key2;
		auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
		node1.work_generate_blocking (*send2);
		node1.process_active (send1);
		node1.block_processor.flush ();
		ASSERT_EQ (1, node1.active.size ());
		std::unique_lock<std::mutex> lock (node1.active.mutex);
		auto existing (node1.active.roots.find (send1->qualified_root ()));
		ASSERT_NE (node1.active.roots.end (), existing);
		auto election (existing->election);
		lock.unlock ();
		system.deadline_set (1s);
		// Wait until the genesis rep activated & makes vote
		while (election->last_votes_size () != 2)
		{
			node1.block_processor.generator.add (send1->hash ());
			node1.vote_processor.flush ();
			ASSERT_NO_ERROR (system.poll ());
		}
		node1.process_active (send2);
		node1.block_processor.flush ();
		lock.lock ();
		auto existing1 (election->last_votes.find (btcb::test_genesis_key.pub));
		ASSERT_NE (election->last_votes.end (), existing1);
		ASSERT_EQ (send1->hash (), existing1->second.hash);
		auto transaction (node1.store.tx_begin_read ());
		auto winner (*election->tally (transaction).begin ());
		ASSERT_EQ (*send1, *winner.second);
		ASSERT_EQ (btcb::genesis_amount - 100, winner.first);
	}
	ASSERT_TRUE (node0.expired ());
}

TEST (node, fork_keep)
{
	btcb::system system (24000, 2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	btcb::keypair key1;
	btcb::keypair key2;
	btcb::genesis genesis;
	// send1 and send2 fork to different accounts
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	node1.process_active (send1);
	node1.block_processor.flush ();
	node2.process_active (send1);
	node2.block_processor.flush ();
	ASSERT_EQ (1, node1.active.size ());
	ASSERT_EQ (1, node2.active.size ());
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	node1.process_active (send2);
	node1.block_processor.flush ();
	node2.process_active (send2);
	node2.block_processor.flush ();
	std::unique_lock<std::mutex> lock (node2.active.mutex);
	auto conflict (node2.active.roots.find (btcb::qualified_root (genesis.hash (), genesis.hash ())));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	lock.unlock ();
	{
		auto transaction0 (system.nodes[0]->store.tx_begin_read ());
		auto transaction1 (system.nodes[1]->store.tx_begin_read ());
		ASSERT_TRUE (system.nodes[0]->store.block_exists (transaction0, send1->hash ()));
		ASSERT_TRUE (system.nodes[1]->store.block_exists (transaction1, send1->hash ()));
	}
	system.deadline_set (1.5min);
	// Wait until the genesis rep makes a vote
	while (votes1->last_votes_size () == 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto transaction0 (system.nodes[0]->store.tx_begin_read ());
	auto transaction1 (system.nodes[1]->store.tx_begin_read ());
	// The vote should be in agreement with what we already have.
	lock.lock ();
	auto winner (*votes1->tally (transaction1).begin ());
	ASSERT_EQ (*send1, *winner.second);
	ASSERT_EQ (btcb::genesis_amount - 100, winner.first);
	ASSERT_TRUE (system.nodes[0]->store.block_exists (transaction0, send1->hash ()));
	ASSERT_TRUE (system.nodes[1]->store.block_exists (transaction1, send1->hash ()));
}

TEST (node, fork_flip)
{
	btcb::system system (24000, 2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	btcb::keypair key1;
	btcb::genesis genesis;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	btcb::publish publish1 (send1);
	btcb::keypair key2;
	auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	btcb::publish publish2 (send2);
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.process_message (publish1, channel1);
	node1.block_processor.flush ();
	auto channel2 (node2.network.udp_channels.create (node1.network.endpoint ()));
	node2.process_message (publish2, channel2);
	node2.block_processor.flush ();
	ASSERT_EQ (1, node1.active.size ());
	ASSERT_EQ (1, node2.active.size ());
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	node1.process_message (publish2, channel1);
	node1.block_processor.flush ();
	node2.process_message (publish1, channel2);
	node2.block_processor.flush ();
	std::unique_lock<std::mutex> lock (node2.active.mutex);
	auto conflict (node2.active.roots.find (btcb::qualified_root (genesis.hash (), genesis.hash ())));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	lock.unlock ();
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, publish1.block->hash ()));
	}
	{
		auto transaction (system.nodes[1]->store.tx_begin_read ());
		ASSERT_TRUE (node2.store.block_exists (transaction, publish2.block->hash ()));
	}
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
		done = node2.ledger.block_exists (publish1.block->hash ());
	}
	auto transaction1 (system.nodes[0]->store.tx_begin_read ());
	auto transaction2 (system.nodes[1]->store.tx_begin_read ());
	lock.lock ();
	auto winner (*votes1->tally (transaction2).begin ());
	ASSERT_EQ (*publish1.block, *winner.second);
	ASSERT_EQ (btcb::genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.store.block_exists (transaction1, publish1.block->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction2, publish1.block->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction2, publish2.block->hash ()));
}

TEST (node, fork_multi_flip)
{
	btcb::system system (24000, 2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	btcb::keypair key1;
	btcb::genesis genesis;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	btcb::publish publish1 (send1);
	btcb::keypair key2;
	auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	btcb::publish publish2 (send2);
	auto send3 (std::make_shared<btcb::send_block> (publish2.block->hash (), key2.pub, btcb::genesis_amount - 100, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (publish2.block->hash ())));
	btcb::publish publish3 (send3);
	node1.process_message (publish1, node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.block_processor.flush ();
	node2.process_message (publish2, node2.network.udp_channels.create (node2.network.endpoint ()));
	node2.process_message (publish3, node2.network.udp_channels.create (node2.network.endpoint ()));
	node2.block_processor.flush ();
	ASSERT_EQ (1, node1.active.size ());
	ASSERT_EQ (2, node2.active.size ());
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	node1.process_message (publish2, node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.process_message (publish3, node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.block_processor.flush ();
	node2.process_message (publish1, node2.network.udp_channels.create (node2.network.endpoint ()));
	node2.block_processor.flush ();
	std::unique_lock<std::mutex> lock (node2.active.mutex);
	auto conflict (node2.active.roots.find (btcb::qualified_root (genesis.hash (), genesis.hash ())));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	lock.unlock ();
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, publish1.block->hash ()));
	}
	{
		auto transaction (system.nodes[1]->store.tx_begin_read ());
		ASSERT_TRUE (node2.store.block_exists (transaction, publish2.block->hash ()));
		ASSERT_TRUE (node2.store.block_exists (transaction, publish3.block->hash ()));
	}
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		ASSERT_NO_ERROR (system.poll ());
		done = node2.ledger.block_exists (publish1.block->hash ());
	}
	auto transaction1 (system.nodes[0]->store.tx_begin_read ());
	auto transaction2 (system.nodes[1]->store.tx_begin_read ());
	lock.lock ();
	auto winner (*votes1->tally (transaction2).begin ());
	ASSERT_EQ (*publish1.block, *winner.second);
	ASSERT_EQ (btcb::genesis_amount - 100, winner.first);
	ASSERT_TRUE (node1.store.block_exists (transaction1, publish1.block->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction2, publish1.block->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction2, publish2.block->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction2, publish3.block->hash ()));
}

// Blocks that are no longer actively being voted on should be able to be evicted through bootstrapping.
// This could happen if a fork wasn't resolved before the process previously shut down
TEST (node, fork_bootstrap_flip)
{
	btcb::system system0 (24000, 1);
	btcb::system system1 (24001, 1);
	auto & node1 (*system0.nodes[0]);
	auto & node2 (*system1.nodes[0]);
	system0.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::block_hash latest (system0.nodes[0]->latest (btcb::test_genesis_key.pub));
	btcb::keypair key1;
	auto send1 (std::make_shared<btcb::send_block> (latest, key1.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system0.work.generate (latest)));
	btcb::keypair key2;
	auto send2 (std::make_shared<btcb::send_block> (latest, key2.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system0.work.generate (latest)));
	// Insert but don't rebroadcast, simulating settled blocks
	node1.block_processor.add (send1, btcb::seconds_since_epoch ());
	node1.block_processor.flush ();
	node2.block_processor.add (send2, btcb::seconds_since_epoch ());
	node2.block_processor.flush ();
	{
		auto transaction (node2.store.tx_begin_read ());
		ASSERT_TRUE (node2.store.block_exists (transaction, send2->hash ()));
	}
	auto channel (std::make_shared<btcb::transport::channel_udp> (node1.network.udp_channels, node2.network.endpoint ()));
	node1.network.send_keepalive (channel);
	system1.deadline_set (50s);
	while (node2.network.empty ())
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint ());
	auto again (true);
	system1.deadline_set (50s);
	while (again)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
		auto transaction (node2.store.tx_begin_read ());
		again = !node2.store.block_exists (transaction, send1->hash ());
	}
}

TEST (node, fork_open)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	btcb::keypair key1;
	btcb::genesis genesis;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	btcb::publish publish1 (send1);
	auto channel1 (node1.network.udp_channels.create (node1.network.endpoint ()));
	node1.process_message (publish1, channel1);
	node1.block_processor.flush ();
	auto open1 (std::make_shared<btcb::open_block> (publish1.block->hash (), 1, key1.pub, key1.prv, key1.pub, system.work.generate (key1.pub)));
	btcb::publish publish2 (open1);
	node1.process_message (publish2, channel1);
	node1.block_processor.flush ();
	auto open2 (std::make_shared<btcb::open_block> (publish1.block->hash (), 2, key1.pub, key1.prv, key1.pub, system.work.generate (key1.pub)));
	btcb::publish publish3 (open2);
	ASSERT_EQ (2, node1.active.size ());
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	node1.process_message (publish3, channel1);
	node1.block_processor.flush ();
}

TEST (node, fork_open_flip)
{
	btcb::system system (24000, 2);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	ASSERT_EQ (1, node1.network.size ());
	btcb::keypair key1;
	btcb::genesis genesis;
	btcb::keypair rep1;
	btcb::keypair rep2;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, btcb::genesis_amount - 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	node1.process_active (send1);
	node2.process_active (send1);
	// We should be keeping this block
	auto open1 (std::make_shared<btcb::open_block> (send1->hash (), rep1.pub, key1.pub, key1.prv, key1.pub, system.work.generate (key1.pub)));
	// This block should be evicted
	auto open2 (std::make_shared<btcb::open_block> (send1->hash (), rep2.pub, key1.pub, key1.prv, key1.pub, system.work.generate (key1.pub)));
	ASSERT_FALSE (*open1 == *open2);
	// node1 gets copy that will remain
	node1.process_active (open1);
	node1.block_processor.flush ();
	// node2 gets copy that will be evicted
	node2.process_active (open2);
	node2.block_processor.flush ();
	ASSERT_EQ (2, node1.active.size ());
	ASSERT_EQ (2, node2.active.size ());
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	// Notify both nodes that a fork exists
	node1.process_active (open2);
	node1.block_processor.flush ();
	node2.process_active (open1);
	node2.block_processor.flush ();
	std::unique_lock<std::mutex> lock (node2.active.mutex);
	auto conflict (node2.active.roots.find (open1->qualified_root ()));
	ASSERT_NE (node2.active.roots.end (), conflict);
	auto votes1 (conflict->election);
	ASSERT_NE (nullptr, votes1);
	ASSERT_EQ (1, votes1->last_votes.size ());
	lock.unlock ();
	ASSERT_TRUE (node1.block (open1->hash ()) != nullptr);
	ASSERT_TRUE (node2.block (open2->hash ()) != nullptr);
	system.deadline_set (10s);
	// Node2 should eventually settle on open1
	while (node2.block (open1->hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	node2.block_processor.flush ();
	auto transaction1 (system.nodes[0]->store.tx_begin_read ());
	auto transaction2 (system.nodes[1]->store.tx_begin_read ());
	lock.lock ();
	auto winner (*votes1->tally (transaction2).begin ());
	ASSERT_EQ (*open1, *winner.second);
	ASSERT_EQ (btcb::genesis_amount - 1, winner.first);
	ASSERT_TRUE (node1.store.block_exists (transaction1, open1->hash ()));
	ASSERT_TRUE (node2.store.block_exists (transaction2, open1->hash ()));
	ASSERT_FALSE (node2.store.block_exists (transaction2, open2->hash ()));
}

TEST (node, coherent_observer)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	node1.observers.blocks.add ([&node1](std::shared_ptr<btcb::block> block_a, btcb::account const &, btcb::uint128_t const &, bool) {
		auto transaction (node1.store.tx_begin_read ());
		ASSERT_TRUE (node1.store.block_exists (transaction, block_a->hash ()));
	});
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::keypair key;
	system.wallet (0)->send_action (btcb::test_genesis_key.pub, key.pub, 1);
}

TEST (node, fork_no_vote_quorum)
{
	btcb::system system (24000, 3);
	auto & node1 (*system.nodes[0]);
	auto & node2 (*system.nodes[1]);
	auto & node3 (*system.nodes[2]);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	auto key4 (system.wallet (0)->deterministic_insert ());
	system.wallet (0)->send_action (btcb::test_genesis_key.pub, key4, btcb::genesis_amount / 4);
	auto key1 (system.wallet (1)->deterministic_insert ());
	{
		auto transaction (system.wallet (1)->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key1);
	}
	auto block (system.wallet (0)->send_action (btcb::test_genesis_key.pub, key1, node1.config.receive_minimum.number ()));
	ASSERT_NE (nullptr, block);
	system.deadline_set (30s);
	while (node3.balance (key1) != node1.config.receive_minimum.number () || node2.balance (key1) != node1.config.receive_minimum.number () || node1.balance (key1) != node1.config.receive_minimum.number ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (node1.config.receive_minimum.number (), node1.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node2.weight (key1));
	ASSERT_EQ (node1.config.receive_minimum.number (), node3.weight (key1));
	btcb::state_block send1 (btcb::test_genesis_key.pub, block->hash (), btcb::test_genesis_key.pub, (btcb::genesis_amount / 4) - (node1.config.receive_minimum.number () * 2), key1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (block->hash ()));
	ASSERT_EQ (btcb::process_result::progress, node1.process (send1).code);
	ASSERT_EQ (btcb::process_result::progress, node2.process (send1).code);
	ASSERT_EQ (btcb::process_result::progress, node3.process (send1).code);
	auto key2 (system.wallet (2)->deterministic_insert ());
	auto send2 (std::make_shared<btcb::send_block> (block->hash (), key2, (btcb::genesis_amount / 4) - (node1.config.receive_minimum.number () * 2), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (block->hash ())));
	btcb::raw_key key3;
	auto transaction (system.wallet (1)->wallets.tx_begin_read ());
	ASSERT_FALSE (system.wallet (1)->store.fetch (transaction, key1, key3));
	auto vote (std::make_shared<btcb::vote> (key1, key3, 0, send2));
	btcb::confirm_ack confirm (vote);
	std::shared_ptr<std::vector<uint8_t>> bytes (new std::vector<uint8_t>);
	{
		btcb::vectorstream stream (*bytes);
		confirm.serialize (stream);
	}
	btcb::transport::channel_udp channel (node2.network.udp_channels, node3.network.endpoint ());
	channel.send_buffer (bytes, btcb::stat::detail::confirm_ack);
	while (node3.stats.count (btcb::stat::type::message, btcb::stat::detail::confirm_ack, btcb::stat::dir::in) < 3)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_TRUE (node1.latest (btcb::test_genesis_key.pub) == send1.hash ());
	ASSERT_TRUE (node2.latest (btcb::test_genesis_key.pub) == send1.hash ());
	ASSERT_TRUE (node3.latest (btcb::test_genesis_key.pub) == send1.hash ());
}

// Disabled because it sometimes takes way too long (but still eventually finishes)
TEST (node, DISABLED_fork_pre_confirm)
{
	btcb::system system (24000, 3);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto & node2 (*system.nodes[2]);
	btcb::genesis genesis;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::keypair key1;
	system.wallet (1)->insert_adhoc (key1.prv);
	{
		auto transaction (system.wallet (1)->wallets.tx_begin_write ());
		system.wallet (1)->store.representative_set (transaction, key1.pub);
	}
	btcb::keypair key2;
	system.wallet (2)->insert_adhoc (key2.prv);
	{
		auto transaction (system.wallet (2)->wallets.tx_begin_write ());
		system.wallet (2)->store.representative_set (transaction, key2.pub);
	}
	system.deadline_set (30s);
	auto block0 (system.wallet (0)->send_action (btcb::test_genesis_key.pub, key1.pub, btcb::genesis_amount / 3));
	ASSERT_NE (nullptr, block0);
	while (node0.balance (key1.pub) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto block1 (system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, btcb::genesis_amount / 3));
	ASSERT_NE (nullptr, block1);
	while (node0.balance (key2.pub) == 0)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	btcb::keypair key3;
	btcb::keypair key4;
	auto block2 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, node0.latest (btcb::test_genesis_key.pub), key3.pub, node0.balance (btcb::test_genesis_key.pub), 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	auto block3 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, node0.latest (btcb::test_genesis_key.pub), key4.pub, node0.balance (btcb::test_genesis_key.pub), 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node0.work_generate_blocking (*block2);
	node0.work_generate_blocking (*block3);
	node0.process_active (block2);
	node1.process_active (block2);
	node2.process_active (block3);
	auto done (false);
	// Extend deadline; we must finish within a total of 100 seconds
	system.deadline_set (70s);
	while (!done)
	{
		done |= node0.latest (btcb::test_genesis_key.pub) == block2->hash () && node1.latest (btcb::test_genesis_key.pub) == block2->hash () && node2.latest (btcb::test_genesis_key.pub) == block2->hash ();
		done |= node0.latest (btcb::test_genesis_key.pub) == block3->hash () && node1.latest (btcb::test_genesis_key.pub) == block3->hash () && node2.latest (btcb::test_genesis_key.pub) == block3->hash ();
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Sometimes hangs on the bootstrap_initiator.bootstrap call
TEST (node, DISABLED_fork_stale)
{
	btcb::system system1 (24000, 1);
	system1.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::system system2 (24001, 1);
	auto & node1 (*system1.nodes[0]);
	auto & node2 (*system2.nodes[0]);
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint ());
	auto channel (std::make_shared<btcb::transport::channel_udp> (node2.network.udp_channels, node1.network.endpoint ()));
	node2.rep_crawler.response (channel, btcb::test_genesis_key.pub, btcb::genesis_amount);
	btcb::genesis genesis;
	btcb::keypair key1;
	btcb::keypair key2;
	auto send3 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Mbcb_ratio, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send3);
	node1.process_active (send3);
	system2.deadline_set (10s);
	while (node2.block (send3->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
	auto send1 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send3->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 2 * btcb::Mbcb_ratio, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	auto send2 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send3->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 2 * btcb::Mbcb_ratio, key2.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	{
		auto transaction1 (node1.store.tx_begin_write ());
		ASSERT_EQ (btcb::process_result::progress, node1.ledger.process (transaction1, *send1).code);
		auto transaction2 (node2.store.tx_begin_write ());
		ASSERT_EQ (btcb::process_result::progress, node2.ledger.process (transaction2, *send2).code);
	}
	node1.process_active (send1);
	node1.process_active (send2);
	node2.process_active (send1);
	node2.process_active (send2);
	node2.bootstrap_initiator.bootstrap (node1.network.endpoint ());
	while (node2.block (send1->hash ()) == nullptr)
	{
		system1.poll ();
		ASSERT_NO_ERROR (system2.poll ());
	}
}

TEST (node, broadcast_elected)
{
	std::vector<btcb::transport::transport_type> types{ btcb::transport::transport_type::tcp, btcb::transport::transport_type::udp };
	for (auto & type : types)
	{
		btcb::system system (24000, 3, type);
		auto node0 (system.nodes[0]);
		auto node1 (system.nodes[1]);
		auto node2 (system.nodes[2]);
		btcb::keypair rep_big;
		btcb::keypair rep_small;
		btcb::keypair rep_other;
		//std::cerr << "Big: " << rep_big.pub.to_account () << std::endl;
		//std::cerr << "Small: " << rep_small.pub.to_account () << std::endl;
		//std::cerr << "Other: " << rep_other.pub.to_account () << std::endl;
		{
			auto transaction0 (node0->store.tx_begin_write ());
			auto transaction1 (node1->store.tx_begin_write ());
			auto transaction2 (node2->store.tx_begin_write ());
			btcb::send_block fund_big (node0->ledger.latest (transaction0, btcb::test_genesis_key.pub), rep_big.pub, btcb::Gbcb_ratio * 5, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
			btcb::open_block open_big (fund_big.hash (), rep_big.pub, rep_big.pub, rep_big.prv, rep_big.pub, 0);
			btcb::send_block fund_small (fund_big.hash (), rep_small.pub, btcb::Gbcb_ratio * 2, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
			btcb::open_block open_small (fund_small.hash (), rep_small.pub, rep_small.pub, rep_small.prv, rep_small.pub, 0);
			btcb::send_block fund_other (fund_small.hash (), rep_other.pub, btcb::Gbcb_ratio * 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
			btcb::open_block open_other (fund_other.hash (), rep_other.pub, rep_other.pub, rep_other.prv, rep_other.pub, 0);
			node0->work_generate_blocking (fund_big);
			node0->work_generate_blocking (open_big);
			node0->work_generate_blocking (fund_small);
			node0->work_generate_blocking (open_small);
			node0->work_generate_blocking (fund_other);
			node0->work_generate_blocking (open_other);
			ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction0, fund_big).code);
			ASSERT_EQ (btcb::process_result::progress, node1->ledger.process (transaction1, fund_big).code);
			ASSERT_EQ (btcb::process_result::progress, node2->ledger.process (transaction2, fund_big).code);
			ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction0, open_big).code);
			ASSERT_EQ (btcb::process_result::progress, node1->ledger.process (transaction1, open_big).code);
			ASSERT_EQ (btcb::process_result::progress, node2->ledger.process (transaction2, open_big).code);
			ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction0, fund_small).code);
			ASSERT_EQ (btcb::process_result::progress, node1->ledger.process (transaction1, fund_small).code);
			ASSERT_EQ (btcb::process_result::progress, node2->ledger.process (transaction2, fund_small).code);
			ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction0, open_small).code);
			ASSERT_EQ (btcb::process_result::progress, node1->ledger.process (transaction1, open_small).code);
			ASSERT_EQ (btcb::process_result::progress, node2->ledger.process (transaction2, open_small).code);
			ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction0, fund_other).code);
			ASSERT_EQ (btcb::process_result::progress, node1->ledger.process (transaction1, fund_other).code);
			ASSERT_EQ (btcb::process_result::progress, node2->ledger.process (transaction2, fund_other).code);
			ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction0, open_other).code);
			ASSERT_EQ (btcb::process_result::progress, node1->ledger.process (transaction1, open_other).code);
			ASSERT_EQ (btcb::process_result::progress, node2->ledger.process (transaction2, open_other).code);
		}
		system.wallet (0)->insert_adhoc (rep_big.prv);
		system.wallet (1)->insert_adhoc (rep_small.prv);
		system.wallet (2)->insert_adhoc (rep_other.prv);
		auto fork0 (std::make_shared<btcb::send_block> (node2->latest (btcb::test_genesis_key.pub), rep_small.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
		node0->work_generate_blocking (*fork0);
		node0->process_active (fork0);
		node1->process_active (fork0);
		auto fork1 (std::make_shared<btcb::send_block> (node2->latest (btcb::test_genesis_key.pub), rep_big.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
		node0->work_generate_blocking (*fork1);
		system.wallet (2)->insert_adhoc (rep_small.prv);
		node2->process_active (fork1);
		//std::cerr << "fork0: " << fork_hash.to_string () << std::endl;
		//std::cerr << "fork1: " << fork1.hash ().to_string () << std::endl;
		while (!node0->ledger.block_exists (fork0->hash ()) || !node1->ledger.block_exists (fork0->hash ()))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		system.deadline_set (50s);
		while (!node2->ledger.block_exists (fork0->hash ()))
		{
			auto ec = system.poll ();
			ASSERT_TRUE (node0->ledger.block_exists (fork0->hash ()));
			ASSERT_TRUE (node1->ledger.block_exists (fork0->hash ()));
			ASSERT_NO_ERROR (ec);
		}
	}
}

TEST (node, rep_self_vote)
{
	btcb::system system (24000, 1);
	auto node0 (system.nodes[0]);
	btcb::keypair rep_big;
	{
		auto transaction0 (node0->store.tx_begin_write ());
		btcb::send_block fund_big (node0->ledger.latest (transaction0, btcb::test_genesis_key.pub), rep_big.pub, btcb::uint128_t ("0xb0000000000000000000000000000000"), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
		btcb::open_block open_big (fund_big.hash (), rep_big.pub, rep_big.pub, rep_big.prv, rep_big.pub, 0);
		node0->work_generate_blocking (fund_big);
		node0->work_generate_blocking (open_big);
		ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction0, fund_big).code);
		ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction0, open_big).code);
	}
	system.wallet (0)->insert_adhoc (rep_big.prv);
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	ASSERT_EQ (system.wallet (0)->wallets.reps_count, 2);
	auto block0 (std::make_shared<btcb::send_block> (node0->latest (btcb::test_genesis_key.pub), rep_big.pub, btcb::uint128_t ("0x60000000000000000000000000000000"), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node0->work_generate_blocking (*block0);
	ASSERT_EQ (btcb::process_result::progress, node0->process (*block0).code);
	auto & active (node0->active);
	active.start (block0);
	std::unique_lock<std::mutex> lock (active.mutex);
	auto existing (active.roots.find (block0->qualified_root ()));
	ASSERT_NE (active.roots.end (), existing);
	auto election (existing->election);
	lock.unlock ();
	system.deadline_set (1s);
	// Wait until representatives are activated & make vote
	while (election->last_votes_size () != 3)
	{
		lock.lock ();
		auto transaction (node0->store.tx_begin_read ());
		election->compute_rep_votes (transaction);
		lock.unlock ();
		node0->vote_processor.flush ();
		ASSERT_NO_ERROR (system.poll ());
	}
	lock.lock ();
	auto & rep_votes (election->last_votes);
	ASSERT_NE (rep_votes.end (), rep_votes.find (btcb::test_genesis_key.pub));
	ASSERT_NE (rep_votes.end (), rep_votes.find (rep_big.pub));
}

// Bootstrapping shouldn't republish the blocks to the network.
TEST (node, DISABLED_bootstrap_no_publish)
{
	btcb::system system0 (24000, 1);
	btcb::system system1 (24001, 1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	btcb::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	btcb::send_block send0 (system0.nodes[0]->latest (btcb::test_genesis_key.pub), key0.pub, 500, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
	{
		auto transaction (node0->store.tx_begin_write ());
		ASSERT_EQ (btcb::process_result::progress, system0.nodes[0]->ledger.process (transaction, send0).code);
	}
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ());
	ASSERT_TRUE (node1->active.empty ());
	system1.deadline_set (10s);
	while (node1->block (send0.hash ()) == nullptr)
	{
		// Poll until the TCP connection is torn down and in_progress goes false
		system0.poll ();
		auto ec = system1.poll ();
		// There should never be an active transaction because the only activity is bootstrapping 1 block which shouldn't be publishing.
		ASSERT_TRUE (node1->active.empty ());
		ASSERT_NO_ERROR (ec);
	}
}

// Check that an outgoing bootstrap request can push blocks
TEST (node, bootstrap_bulk_push)
{
	btcb::system system0 (24000, 1);
	btcb::system system1 (24001, 1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	btcb::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	btcb::send_block send0 (system0.nodes[0]->latest (btcb::test_genesis_key.pub), key0.pub, 500, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
	node0->work_generate_blocking (send0);
	{
		auto transaction (node0->store.tx_begin_write ());
		ASSERT_EQ (btcb::process_result::progress, system0.nodes[0]->ledger.process (transaction, send0).code);
	}
	ASSERT_FALSE (node0->bootstrap_initiator.in_progress ());
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->active.empty ());
	node0->bootstrap_initiator.bootstrap (node1->network.endpoint (), false);
	system1.deadline_set (10s);
	while (node1->block (send0.hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	// since this uses bulk_push, the new block should be republished
	ASSERT_FALSE (node1->active.empty ());
}

// Bootstrapping a forked open block should succeed.
TEST (node, bootstrap_fork_open)
{
	btcb::system system0 (24000, 2);
	system0.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	auto node0 (system0.nodes[0]);
	auto node1 (system0.nodes[1]);
	btcb::keypair key0;
	btcb::send_block send0 (system0.nodes[0]->latest (btcb::test_genesis_key.pub), key0.pub, btcb::genesis_amount - 500, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
	btcb::open_block open0 (send0.hash (), 1, key0.pub, key0.prv, key0.pub, 0);
	btcb::open_block open1 (send0.hash (), 2, key0.pub, key0.prv, key0.pub, 0);
	node0->work_generate_blocking (send0);
	node0->work_generate_blocking (open0);
	node0->work_generate_blocking (open1);
	{
		auto transaction0 (node0->store.tx_begin_write ());
		auto transaction1 (node1->store.tx_begin_write ());
		// Both know about send0
		ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction0, send0).code);
		ASSERT_EQ (btcb::process_result::progress, node1->ledger.process (transaction1, send0).code);
		// They disagree about open0/open1
		ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction0, open0).code);
		ASSERT_EQ (btcb::process_result::progress, node1->ledger.process (transaction1, open1).code);
	}
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ());
	ASSERT_TRUE (node1->active.empty ());
	system0.deadline_set (10s);
	while (node1->ledger.block_exists (open1.hash ()))
	{
		// Poll until the outvoted block is evicted.
		ASSERT_NO_ERROR (system0.poll ());
	}
}

// Unconfirmed blocks from bootstrap should be confirmed
TEST (node, bootstrap_confirm_frontiers)
{
	btcb::system system0 (24000, 1);
	btcb::system system1 (24001, 1);
	auto node0 (system0.nodes[0]);
	auto node1 (system1.nodes[0]);
	system0.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::keypair key0;
	// node0 knows about send0 but node1 doesn't.
	btcb::send_block send0 (node0->latest (btcb::test_genesis_key.pub), key0.pub, btcb::genesis_amount - 500, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0);
	node0->work_generate_blocking (send0);
	{
		auto transaction (node0->store.tx_begin_write ());
		ASSERT_EQ (btcb::process_result::progress, node0->ledger.process (transaction, send0).code);
	}
	ASSERT_FALSE (node0->bootstrap_initiator.in_progress ());
	ASSERT_FALSE (node1->bootstrap_initiator.in_progress ());
	ASSERT_TRUE (node1->active.empty ());
	node1->bootstrap_initiator.bootstrap (node0->network.endpoint ());
	system1.deadline_set (10s);
	while (node1->block (send0.hash ()) == nullptr)
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	// Wait for election start
	system1.deadline_set (10s);
	while (node1->active.empty ())
	{
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
	{
		std::lock_guard<std::mutex> guard (node1->active.mutex);
		auto existing1 (node1->active.blocks.find (send0.hash ()));
		ASSERT_NE (node1->active.blocks.end (), existing1);
	}
	// Wait for confirmation height update
	system1.deadline_set (10s);
	bool done (false);
	while (!done)
	{
		{
			auto transaction (node1->store.tx_begin_read ());
			done = node1->ledger.block_confirmed (transaction, send0.hash ());
		}
		ASSERT_NO_ERROR (system0.poll ());
		ASSERT_NO_ERROR (system1.poll ());
	}
}

// Test that if we create a block that isn't confirmed, we sync.
TEST (node, DISABLED_unconfirmed_send)
{
	btcb::system system (24000, 2);
	auto & node0 (*system.nodes[0]);
	auto & node1 (*system.nodes[1]);
	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	btcb::keypair key0;
	wallet1->insert_adhoc (key0.prv);
	wallet0->insert_adhoc (btcb::test_genesis_key.prv);
	auto send1 (wallet0->send_action (btcb::genesis_account, key0.pub, 2 * btcb::Mbcb_ratio));
	system.deadline_set (10s);
	while (node1.balance (key0.pub) != 2 * btcb::Mbcb_ratio || node1.bootstrap_initiator.in_progress ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto latest (node1.latest (key0.pub));
	btcb::state_block send2 (key0.pub, latest, btcb::genesis_account, btcb::Mbcb_ratio, btcb::genesis_account, key0.prv, key0.pub, node0.work_generate_blocking (latest));
	{
		auto transaction (node1.store.tx_begin_write ());
		ASSERT_EQ (btcb::process_result::progress, node1.ledger.process (transaction, send2).code);
	}
	auto send3 (wallet1->send_action (key0.pub, btcb::genesis_account, btcb::Mbcb_ratio));
	system.deadline_set (10s);
	while (node0.balance (btcb::genesis_account) != btcb::genesis_amount)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

// Test that nodes can track nodes that have rep weight for priority broadcasting
TEST (node, rep_list)
{
	btcb::system system (24000, 2);
	auto & node1 (*system.nodes[1]);
	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	// Node0 has a rep
	wallet0->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::keypair key1;
	// Broadcast a confirm so others should know this is a rep node
	wallet0->send_action (btcb::test_genesis_key.pub, key1.pub, btcb::Mbcb_ratio);
	ASSERT_EQ (0, node1.rep_crawler.representatives (1).size ());
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto reps (node1.rep_crawler.representatives (1));
		if (!reps.empty ())
		{
			if (!reps[0].weight.is_zero ())
			{
				done = true;
			}
		}
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, rep_weight)
{
	btcb::system system (24000, 1);
	auto & node (*system.nodes[0]);

	node.network.udp_channels.insert (btcb::endpoint (boost::asio::ip::address_v6::loopback (), 24001), 0);
	ASSERT_TRUE (node.rep_crawler.representatives (1).empty ());
	btcb::endpoint endpoint0 (boost::asio::ip::address_v6::loopback (), 24000);
	btcb::endpoint endpoint1 (boost::asio::ip::address_v6::loopback (), 24002);
	btcb::endpoint endpoint2 (boost::asio::ip::address_v6::loopback (), 24003);
	auto channel0 (std::make_shared<btcb::transport::channel_udp> (node.network.udp_channels, endpoint0));
	auto channel1 (std::make_shared<btcb::transport::channel_udp> (node.network.udp_channels, endpoint1));
	auto channel2 (std::make_shared<btcb::transport::channel_udp> (node.network.udp_channels, endpoint2));
	btcb::amount amount100 (100);
	btcb::amount amount50 (50);
	node.network.udp_channels.insert (endpoint2, btcb::protocol_version);
	node.network.udp_channels.insert (endpoint0, btcb::protocol_version);
	node.network.udp_channels.insert (endpoint1, btcb::protocol_version);
	btcb::keypair keypair1;
	btcb::keypair keypair2;
	node.rep_crawler.response (channel0, keypair1.pub, amount100);
	node.rep_crawler.response (channel1, keypair2.pub, amount50);
	ASSERT_EQ (2, node.rep_crawler.representative_count ());
	// Make sure we get the rep with the most weight first
	auto reps (node.rep_crawler.representatives (1));
	ASSERT_EQ (1, reps.size ());
	ASSERT_EQ (100, reps[0].weight.number ());
	ASSERT_EQ (keypair1.pub, reps[0].account);
	ASSERT_EQ (*channel0, reps[0].channel_ref ());
}

// Test that nodes can disable representative voting
TEST (node, no_voting)
{
	btcb::system system (24000, 1);
	auto & node0 (*system.nodes[0]);
	btcb::node_config node_config (24001, system.logging);
	node_config.enable_voting = false;
	system.add_node (node_config);

	auto wallet0 (system.wallet (0));
	auto wallet1 (system.wallet (1));
	// Node1 has a rep
	wallet1->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::keypair key1;
	wallet1->insert_adhoc (key1.prv);
	// Broadcast a confirm so others should know this is a rep node
	wallet1->send_action (btcb::test_genesis_key.pub, key1.pub, btcb::Mbcb_ratio);
	system.deadline_set (10s);
	while (!node0.active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, node0.stats.count (btcb::stat::type::message, btcb::stat::detail::confirm_ack, btcb::stat::dir::in));
}

TEST (node, send_callback)
{
	btcb::system system (24000, 1);
	btcb::keypair key2;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	system.wallet (0)->insert_adhoc (key2.prv);
	system.nodes[0]->config.callback_address = "localhost";
	system.nodes[0]->config.callback_port = 8010;
	system.nodes[0]->config.callback_target = "/";
	ASSERT_NE (nullptr, system.wallet (0)->send_action (btcb::test_genesis_key.pub, key2.pub, system.nodes[0]->config.receive_minimum.number ()));
	system.deadline_set (10s);
	while (system.nodes[0]->balance (key2.pub).is_zero ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), system.nodes[0]->balance (btcb::test_genesis_key.pub));
}

// Check that votes get replayed back to nodes if they sent an old sequence number.
// This helps representatives continue from their last sequence number if their node is reinitialized and the old sequence number is lost
TEST (node, vote_replay)
{
	btcb::system system (24000, 2);
	btcb::keypair key;
	auto open (std::make_shared<btcb::open_block> (0, 1, key.pub, key.prv, key.pub, 0));
	system.nodes[0]->work_generate_blocking (*open);
	for (auto i (0); i < 11000; ++i)
	{
		auto transaction (system.nodes[1]->store.tx_begin_read ());
		auto vote (system.nodes[1]->store.vote_generate (transaction, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, open));
	}
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		std::lock_guard<std::mutex> lock (boost::polymorphic_downcast<btcb::mdb_store *> (system.nodes[0]->store_impl.get ())->cache_mutex);
		auto vote (system.nodes[0]->store.vote_current (transaction, btcb::test_genesis_key.pub));
		ASSERT_EQ (nullptr, vote);
	}
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	auto block (system.wallet (0)->send_action (btcb::test_genesis_key.pub, key.pub, btcb::Gbcb_ratio));
	ASSERT_NE (nullptr, block);
	auto done (false);
	system.deadline_set (20s);
	while (!done)
	{
		auto ec = system.poll ();
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		std::lock_guard<std::mutex> lock (boost::polymorphic_downcast<btcb::mdb_store *> (system.nodes[0]->store_impl.get ())->cache_mutex);
		auto vote (system.nodes[0]->store.vote_current (transaction, btcb::test_genesis_key.pub));
		done = vote && (vote->sequence >= 10000);
		ASSERT_NO_ERROR (ec);
	}
}

TEST (node, balance_observer)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	std::atomic<int> balances (0);
	btcb::keypair key;
	node1.observers.account_balance.add ([&key, &balances](btcb::account const & account_a, bool is_pending) {
		if (key.pub == account_a && is_pending)
		{
			balances++;
		}
		else if (btcb::test_genesis_key.pub == account_a && !is_pending)
		{
			balances++;
		}
	});
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	system.wallet (0)->send_action (btcb::test_genesis_key.pub, key.pub, 1);
	system.deadline_set (10s);
	auto done (false);
	while (!done)
	{
		auto ec = system.poll ();
		done = balances.load () == 2;
		ASSERT_NO_ERROR (ec);
	}
}

// ASSERT_NE (nullptr, attempt) sometimes fails
TEST (node, DISABLED_bootstrap_connection_scaling)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	node1.bootstrap_initiator.bootstrap ();
	auto attempt (node1.bootstrap_initiator.current_attempt ());
	ASSERT_NE (nullptr, attempt);
	ASSERT_EQ (34, attempt->target_connections (25000));
	ASSERT_EQ (4, attempt->target_connections (0));
	ASSERT_EQ (64, attempt->target_connections (50000));
	ASSERT_EQ (64, attempt->target_connections (10000000000));
	node1.config.bootstrap_connections = 128;
	ASSERT_EQ (64, attempt->target_connections (0));
	ASSERT_EQ (64, attempt->target_connections (50000));
	node1.config.bootstrap_connections_max = 256;
	ASSERT_EQ (128, attempt->target_connections (0));
	ASSERT_EQ (256, attempt->target_connections (50000));
	node1.config.bootstrap_connections_max = 0;
	ASSERT_EQ (1, attempt->target_connections (0));
	ASSERT_EQ (1, attempt->target_connections (50000));
}

// Test stat counting at both type and detail levels
TEST (node, stat_counting)
{
	btcb::system system (24000, 1);
	auto & node1 (*system.nodes[0]);
	node1.stats.add (btcb::stat::type::ledger, btcb::stat::dir::in, 1);
	node1.stats.add (btcb::stat::type::ledger, btcb::stat::dir::in, 5);
	node1.stats.inc (btcb::stat::type::ledger, btcb::stat::dir::in);
	node1.stats.inc (btcb::stat::type::ledger, btcb::stat::detail::send, btcb::stat::dir::in);
	node1.stats.inc (btcb::stat::type::ledger, btcb::stat::detail::send, btcb::stat::dir::in);
	node1.stats.inc (btcb::stat::type::ledger, btcb::stat::detail::receive, btcb::stat::dir::in);
	ASSERT_EQ (10, node1.stats.count (btcb::stat::type::ledger, btcb::stat::dir::in));
	ASSERT_EQ (2, node1.stats.count (btcb::stat::type::ledger, btcb::stat::detail::send, btcb::stat::dir::in));
	ASSERT_EQ (1, node1.stats.count (btcb::stat::type::ledger, btcb::stat::detail::receive, btcb::stat::dir::in));
}

TEST (node, online_reps)
{
	btcb::system system (24000, 1);
	// 1 sample of minimum weight
	ASSERT_EQ (system.nodes[0]->config.online_weight_minimum, system.nodes[0]->online_reps.online_stake ());
	auto vote (std::make_shared<btcb::vote> ());
	system.nodes[0]->online_reps.observe (btcb::test_genesis_key.pub);
	// 1 minimum, 1 maximum
	system.nodes[0]->online_reps.sample ();
	ASSERT_EQ (btcb::genesis_amount, system.nodes[0]->online_reps.online_stake ());
	// 2 minimum, 1 maximum
	system.nodes[0]->online_reps.sample ();
	ASSERT_EQ (system.nodes[0]->config.online_weight_minimum, system.nodes[0]->online_reps.online_stake ());
}

TEST (node, block_confirm)
{
	std::vector<btcb::transport::transport_type> types{ btcb::transport::transport_type::tcp, btcb::transport::transport_type::udp };
	for (auto & type : types)
	{
		btcb::system system (24000, 2, type);
		btcb::genesis genesis;
		btcb::keypair key;
		system.wallet (1)->insert_adhoc (btcb::test_genesis_key.prv);
		auto send1 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (genesis.hash ())));
		system.nodes[0]->block_processor.add (send1, btcb::seconds_since_epoch ());
		system.nodes[1]->block_processor.add (send1, btcb::seconds_since_epoch ());
		system.deadline_set (std::chrono::seconds (5));
		while (!system.nodes[0]->ledger.block_exists (send1->hash ()) || !system.nodes[1]->ledger.block_exists (send1->hash ()))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		ASSERT_TRUE (system.nodes[0]->ledger.block_exists (send1->hash ()));
		ASSERT_TRUE (system.nodes[1]->ledger.block_exists (send1->hash ()));
		auto send2 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send1->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio * 2, key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (send1->hash ())));
		{
			auto transaction (system.nodes[0]->store.tx_begin_write ());
			ASSERT_EQ (btcb::process_result::progress, system.nodes[0]->ledger.process (transaction, *send2).code);
		}
		{
			auto transaction (system.nodes[1]->store.tx_begin_write ());
			ASSERT_EQ (btcb::process_result::progress, system.nodes[1]->ledger.process (transaction, *send2).code);
		}
		system.nodes[0]->block_confirm (send2);
		ASSERT_TRUE (system.nodes[0]->active.list_confirmed ().empty ());
		system.deadline_set (10s);
		while (system.nodes[0]->active.list_confirmed ().empty ())
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}
}

TEST (node, block_arrival)
{
	btcb::system system (24000, 1);
	auto & node (*system.nodes[0]);
	ASSERT_EQ (0, node.block_arrival.arrival.size ());
	btcb::block_hash hash1 (1);
	node.block_arrival.add (hash1);
	ASSERT_EQ (1, node.block_arrival.arrival.size ());
	node.block_arrival.add (hash1);
	ASSERT_EQ (1, node.block_arrival.arrival.size ());
	btcb::block_hash hash2 (2);
	node.block_arrival.add (hash2);
	ASSERT_EQ (2, node.block_arrival.arrival.size ());
}

TEST (node, block_arrival_size)
{
	btcb::system system (24000, 1);
	auto & node (*system.nodes[0]);
	auto time (std::chrono::steady_clock::now () - btcb::block_arrival::arrival_time_min - std::chrono::seconds (5));
	btcb::block_hash hash (0);
	for (auto i (0); i < btcb::block_arrival::arrival_size_min * 2; ++i)
	{
		node.block_arrival.arrival.insert (btcb::block_arrival_info{ time, hash });
		++hash.qwords[0];
	}
	ASSERT_EQ (btcb::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
	node.block_arrival.recent (0);
	ASSERT_EQ (btcb::block_arrival::arrival_size_min, node.block_arrival.arrival.size ());
}

TEST (node, block_arrival_time)
{
	btcb::system system (24000, 1);
	auto & node (*system.nodes[0]);
	auto time (std::chrono::steady_clock::now ());
	btcb::block_hash hash (0);
	for (auto i (0); i < btcb::block_arrival::arrival_size_min * 2; ++i)
	{
		node.block_arrival.arrival.insert (btcb::block_arrival_info{ time, hash });
		++hash.qwords[0];
	}
	ASSERT_EQ (btcb::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
	node.block_arrival.recent (0);
	ASSERT_EQ (btcb::block_arrival::arrival_size_min * 2, node.block_arrival.arrival.size ());
}

TEST (node, confirm_quorum)
{
	btcb::system system (24000, 1);
	btcb::genesis genesis;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	// Put greater than online_weight_minimum in pending so quorum can't be reached
	btcb::uint128_union new_balance (system.nodes[0]->config.online_weight_minimum.number () - btcb::Gbcb_ratio);
	auto send1 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, new_balance, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.nodes[0]->work_generate_blocking (genesis.hash ())));
	{
		auto transaction (system.nodes[0]->store.tx_begin_write ());
		ASSERT_EQ (btcb::process_result::progress, system.nodes[0]->ledger.process (transaction, *send1).code);
	}
	system.wallet (0)->send_action (btcb::test_genesis_key.pub, btcb::test_genesis_key.pub, new_balance.number ());
	system.deadline_set (10s);
	while (system.nodes[0]->active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto done (false);
	while (!done)
	{
		ASSERT_FALSE (system.nodes[0]->active.empty ());
		{
			std::lock_guard<std::mutex> guard (system.nodes[0]->active.mutex);
			auto info (system.nodes[0]->active.roots.find (btcb::qualified_root (send1->hash (), send1->hash ())));
			ASSERT_NE (system.nodes[0]->active.roots.end (), info);
			done = info->election->announcements > btcb::active_transactions::announcement_min;
		}
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (0, system.nodes[0]->balance (btcb::test_genesis_key.pub));
}

TEST (node, local_votes_cache)
{
	btcb::system system (24000, 1);
	auto & node (*system.nodes[0]);
	btcb::genesis genesis;
	system.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	auto send1 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, node.work_generate_blocking (genesis.hash ())));
	auto send2 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send1->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 2 * btcb::Gbcb_ratio, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, node.work_generate_blocking (send1->hash ())));
	auto send3 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send2->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 3 * btcb::Gbcb_ratio, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, node.work_generate_blocking (send2->hash ())));
	{
		auto transaction (node.store.tx_begin_write ());
		ASSERT_EQ (btcb::process_result::progress, node.ledger.process (transaction, *send1).code);
		ASSERT_EQ (btcb::process_result::progress, node.ledger.process (transaction, *send2).code);
	}
	btcb::confirm_req message1 (send1);
	btcb::confirm_req message2 (send2);
	auto channel (node.network.udp_channels.create (node.network.endpoint ()));
	for (auto i (0); i < 100; ++i)
	{
		node.process_message (message1, channel);
		node.process_message (message2, channel);
	}
	{
		std::lock_guard<std::mutex> lock (boost::polymorphic_downcast<btcb::mdb_store *> (node.store_impl.get ())->cache_mutex);
		auto transaction (node.store.tx_begin_read ());
		auto current_vote (node.store.vote_current (transaction, btcb::test_genesis_key.pub));
		ASSERT_EQ (current_vote->sequence, 2);
	}
	// Max cache
	{
		auto transaction (node.store.tx_begin_write ());
		ASSERT_EQ (btcb::process_result::progress, node.ledger.process (transaction, *send3).code);
	}
	btcb::confirm_req message3 (send3);
	for (auto i (0); i < 100; ++i)
	{
		node.process_message (message3, channel);
	}
	{
		std::lock_guard<std::mutex> lock (boost::polymorphic_downcast<btcb::mdb_store *> (node.store_impl.get ())->cache_mutex);
		auto transaction (node.store.tx_begin_read ());
		auto current_vote (node.store.vote_current (transaction, btcb::test_genesis_key.pub));
		ASSERT_EQ (current_vote->sequence, 3);
	}
	ASSERT_TRUE (node.votes_cache.find (send1->hash ()).empty ());
	ASSERT_FALSE (node.votes_cache.find (send2->hash ()).empty ());
	ASSERT_FALSE (node.votes_cache.find (send3->hash ()).empty ());
}

TEST (node, vote_republish)
{
	btcb::system system (24000, 2);
	btcb::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	btcb::genesis genesis;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 2, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	system.nodes[0]->process_active (send1);
	system.deadline_set (5s);
	while (!system.nodes[1]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.nodes[0]->active.publish (send2);
	auto vote (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 0, send2));
	ASSERT_TRUE (system.nodes[0]->active.active (*send1));
	ASSERT_TRUE (system.nodes[1]->active.active (*send1));
	system.nodes[0]->vote_processor.vote (vote, std::make_shared<btcb::transport::channel_udp> (system.nodes[0]->network.udp_channels, system.nodes[0]->network.endpoint ()));
	while (!system.nodes[0]->block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!system.nodes[1]->block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_FALSE (system.nodes[0]->block (send1->hash ()));
	ASSERT_FALSE (system.nodes[1]->block (send1->hash ()));
	system.deadline_set (5s);
	while (system.nodes[1]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number () * 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (system.nodes[0]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number () * 2)
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, vote_by_hash_republish)
{
	std::vector<btcb::transport::transport_type> types{ btcb::transport::transport_type::tcp, btcb::transport::transport_type::udp };
	for (auto & type : types)
	{
		btcb::system system (24000, 2, type);
		btcb::keypair key2;
		system.wallet (1)->insert_adhoc (key2.prv);
		btcb::genesis genesis;
		auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
		auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 2, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
		system.nodes[0]->process_active (send1);
		system.deadline_set (5s);
		while (!system.nodes[1]->block (send1->hash ()))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		system.nodes[0]->active.publish (send2);
		std::vector<btcb::block_hash> vote_blocks;
		vote_blocks.push_back (send2->hash ());
		auto vote (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 0, vote_blocks));
		ASSERT_TRUE (system.nodes[0]->active.active (*send1));
		ASSERT_TRUE (system.nodes[1]->active.active (*send1));
		system.nodes[0]->vote_processor.vote (vote, std::make_shared<btcb::transport::channel_udp> (system.nodes[0]->network.udp_channels, system.nodes[0]->network.endpoint ()));
		while (!system.nodes[0]->block (send2->hash ()))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		while (!system.nodes[1]->block (send2->hash ()))
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		ASSERT_FALSE (system.nodes[0]->block (send1->hash ()));
		ASSERT_FALSE (system.nodes[1]->block (send1->hash ()));
		system.deadline_set (5s);
		while (system.nodes[1]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number () * 2)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		while (system.nodes[0]->balance (key2.pub) != system.nodes[0]->config.receive_minimum.number () * 2)
		{
			ASSERT_NO_ERROR (system.poll ());
		}
	}
}

TEST (node, vote_by_hash_epoch_block_republish)
{
	btcb::system system (24000, 2);
	btcb::keypair key2;
	system.wallet (1)->insert_adhoc (key2.prv);
	btcb::keypair epoch_signer;
	system.nodes[0]->ledger.epoch_signer = epoch_signer.pub;
	system.nodes[1]->ledger.epoch_signer = epoch_signer.pub;
	btcb::genesis genesis;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto epoch1 (std::make_shared<btcb::state_block> (btcb::genesis_account, genesis.hash (), btcb::genesis_account, btcb::genesis_amount, system.nodes[0]->ledger.epoch_link, epoch_signer.prv, epoch_signer.pub, system.work.generate (genesis.hash ())));
	system.nodes[0]->process_active (send1);
	system.deadline_set (5s);
	while (!system.nodes[1]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.nodes[0]->active.publish (epoch1);
	std::vector<btcb::block_hash> vote_blocks;
	vote_blocks.push_back (epoch1->hash ());
	auto vote (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 0, vote_blocks));
	ASSERT_TRUE (system.nodes[0]->active.active (*send1));
	ASSERT_TRUE (system.nodes[1]->active.active (*send1));
	system.nodes[0]->vote_processor.vote (vote, std::make_shared<btcb::transport::channel_udp> (system.nodes[0]->network.udp_channels, system.nodes[0]->network.endpoint ()));
	while (!system.nodes[0]->block (epoch1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!system.nodes[1]->block (epoch1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_FALSE (system.nodes[0]->block (send1->hash ()));
	ASSERT_FALSE (system.nodes[1]->block (send1->hash ()));
}

TEST (node, fork_invalid_block_signature)
{
	btcb::system system (24000, 2);
	btcb::keypair key2;
	btcb::genesis genesis;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 2, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2_corrupt (std::make_shared<btcb::send_block> (*send2));
	send2_corrupt->signature = btcb::signature (123);
	system.nodes[0]->process_active (send1);
	system.deadline_set (5s);
	while (!system.nodes[0]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto vote (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 0, send2));
	auto vote_corrupt (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 0, send2_corrupt));
	system.nodes[1]->network.flood_vote (vote_corrupt);
	ASSERT_NO_ERROR (system.poll ());
	system.nodes[1]->network.flood_vote (vote);
	while (system.nodes[0]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!system.nodes[0]->block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (system.nodes[0]->block (send2->hash ())->block_signature (), send2->block_signature ());
}

TEST (node, fork_invalid_block_signature_vote_by_hash)
{
	btcb::system system (24000, 1);
	btcb::keypair key2;
	btcb::genesis genesis;
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number (), btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2 (std::make_shared<btcb::send_block> (genesis.hash (), key2.pub, std::numeric_limits<btcb::uint128_t>::max () - system.nodes[0]->config.receive_minimum.number () * 2, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto send2_corrupt (std::make_shared<btcb::send_block> (*send2));
	send2_corrupt->signature = btcb::signature (123);
	system.nodes[0]->process_active (send1);
	system.deadline_set (5s);
	while (!system.nodes[0]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	system.nodes[0]->active.publish (send2_corrupt);
	ASSERT_NO_ERROR (system.poll ());
	system.nodes[0]->active.publish (send2);
	std::vector<btcb::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 0, vote_blocks));
	{
		auto transaction (system.nodes[0]->store.tx_begin_read ());
		std::unique_lock<std::mutex> lock (system.nodes[0]->active.mutex);
		system.nodes[0]->vote_processor.vote_blocking (transaction, vote, std::make_shared<btcb::transport::channel_udp> (system.nodes[0]->network.udp_channels, system.nodes[0]->network.endpoint ()));
	}
	while (system.nodes[0]->block (send1->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	while (!system.nodes[0]->block (send2->hash ()))
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	ASSERT_EQ (system.nodes[0]->block (send2->hash ())->block_signature (), send2->block_signature ());
}

TEST (node, block_processor_signatures)
{
	btcb::system system0 (24000, 1);
	auto & node1 (*system0.nodes[0]);
	system0.wallet (0)->insert_adhoc (btcb::test_genesis_key.prv);
	btcb::block_hash latest (system0.nodes[0]->latest (btcb::test_genesis_key.pub));
	btcb::keypair key1;
	auto send1 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, latest, btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, key1.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	btcb::keypair key2;
	auto send2 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send1->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 2 * btcb::Gbcb_ratio, key2.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send2);
	btcb::keypair key3;
	auto send3 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send2->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 3 * btcb::Gbcb_ratio, key3.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send3);
	// Invalid signature bit
	auto send4 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send3->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 4 * btcb::Gbcb_ratio, key3.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send4);
	send4->signature.bytes[32] ^= 0x1;
	// Invalid signature bit (force)
	auto send5 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, send3->hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 5 * btcb::Gbcb_ratio, key3.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send5);
	send5->signature.bytes[31] ^= 0x1;
	// Invalid signature to unchecked
	{
		auto transaction (node1.store.tx_begin_write ());
		node1.store.unchecked_put (transaction, send5->previous (), send5);
	}
	auto receive1 (std::make_shared<btcb::state_block> (key1.pub, 0, btcb::test_genesis_key.pub, btcb::Gbcb_ratio, send1->hash (), key1.prv, key1.pub, 0));
	node1.work_generate_blocking (*receive1);
	auto receive2 (std::make_shared<btcb::state_block> (key2.pub, 0, btcb::test_genesis_key.pub, btcb::Gbcb_ratio, send2->hash (), key2.prv, key2.pub, 0));
	node1.work_generate_blocking (*receive2);
	// Invalid private key
	auto receive3 (std::make_shared<btcb::state_block> (key3.pub, 0, btcb::test_genesis_key.pub, btcb::Gbcb_ratio, send3->hash (), key2.prv, key3.pub, 0));
	node1.work_generate_blocking (*receive3);
	node1.process_active (send1);
	node1.process_active (send2);
	node1.process_active (send3);
	node1.process_active (send4);
	node1.process_active (receive1);
	node1.process_active (receive2);
	node1.process_active (receive3);
	node1.block_processor.flush ();
	node1.block_processor.force (send5);
	node1.block_processor.flush ();
	auto transaction (node1.store.tx_begin_read ());
	ASSERT_TRUE (node1.store.block_exists (transaction, send1->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, send2->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, send3->hash ()));
	ASSERT_FALSE (node1.store.block_exists (transaction, send4->hash ()));
	ASSERT_FALSE (node1.store.block_exists (transaction, send5->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, receive1->hash ()));
	ASSERT_TRUE (node1.store.block_exists (transaction, receive2->hash ()));
	ASSERT_FALSE (node1.store.block_exists (transaction, receive3->hash ()));
}

/*
 *  State blocks go through a different signature path, ensure invalidly signed state blocks are rejected
 */
TEST (node, block_processor_reject_state)
{
	btcb::system system (24000, 1);
	auto & node (*system.nodes[0]);
	btcb::genesis genesis;
	auto send1 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send1);
	send1->signature.bytes[0] ^= 1;
	ASSERT_FALSE (node.ledger.block_exists (send1->hash ()));
	node.process_active (send1);
	node.block_processor.flush ();
	ASSERT_FALSE (node.ledger.block_exists (send1->hash ()));
	auto send2 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 2 * btcb::Gbcb_ratio, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send2);
	node.process_active (send2);
	node.block_processor.flush ();
	ASSERT_TRUE (node.ledger.block_exists (send2->hash ()));
}

TEST (node, block_processor_reject_rolled_back)
{
	btcb::system system (24000, 1);
	auto & node (*system.nodes[0]);
	btcb::genesis genesis;
	auto send1 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - btcb::Gbcb_ratio, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send1);
	node.block_processor.add (send1);
	node.block_processor.flush ();
	ASSERT_TRUE (node.ledger.block_exists (send1->hash ()));
	auto send2 (std::make_shared<btcb::state_block> (btcb::test_genesis_key.pub, genesis.hash (), btcb::test_genesis_key.pub, btcb::genesis_amount - 2 * btcb::Gbcb_ratio, btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node.work_generate_blocking (*send2);
	// Force block send2 & rolling back block send1
	node.block_processor.force (send2);
	node.block_processor.flush ();
	ASSERT_FALSE (node.ledger.block_exists (send1->hash ()));
	ASSERT_TRUE (node.ledger.block_exists (send2->hash ()));
	ASSERT_TRUE (node.active.empty ());
	// Block send1 cannot be processed & start fork resolution election
	node.block_processor.add (send1);
	node.block_processor.flush ();
	ASSERT_FALSE (node.ledger.block_exists (send1->hash ()));
	ASSERT_TRUE (node.active.empty ());
}

TEST (node, confirm_back)
{
	btcb::system system (24000, 1);
	btcb::keypair key;
	auto & node (*system.nodes[0]);
	btcb::genesis genesis;
	auto genesis_start_balance (node.balance (btcb::test_genesis_key.pub));
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key.pub, genesis_start_balance - 1, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, system.work.generate (genesis.hash ())));
	auto open (std::make_shared<btcb::state_block> (key.pub, 0, key.pub, 1, send1->hash (), key.prv, key.pub, system.work.generate (key.pub)));
	auto send2 (std::make_shared<btcb::state_block> (key.pub, open->hash (), key.pub, 0, btcb::test_genesis_key.pub, key.prv, key.pub, system.work.generate (open->hash ())));
	node.process_active (send1);
	node.process_active (open);
	node.process_active (send2);
	node.block_processor.flush ();
	ASSERT_EQ (3, node.active.size ());
	std::vector<btcb::block_hash> vote_blocks;
	vote_blocks.push_back (send2->hash ());
	auto vote (std::make_shared<btcb::vote> (btcb::test_genesis_key.pub, btcb::test_genesis_key.prv, 0, vote_blocks));
	{
		auto transaction (node.store.tx_begin_read ());
		std::unique_lock<std::mutex> lock (node.active.mutex);
		node.vote_processor.vote_blocking (transaction, vote, std::make_shared<btcb::transport::channel_udp> (node.network.udp_channels, node.network.endpoint ()));
	}
	system.deadline_set (10s);
	while (!node.active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
}

TEST (node, peers)
{
	btcb::system system (24000, 1);
	ASSERT_TRUE (system.nodes.front ()->network.empty ());

	btcb::node_init init;
	auto node (std::make_shared<btcb::node> (init, system.io_ctx, 24001, btcb::unique_path (), system.alarm, system.logging, system.work));
	system.nodes.push_back (node);

	auto endpoint = system.nodes.front ()->network.endpoint ();
	btcb::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto & store = system.nodes.back ()->store;
	{
		// Add a peer to the database
		auto transaction (store.tx_begin_write ());
		store.peer_put (transaction, endpoint_key);

		// Add a peer which is not contactable
		store.peer_put (transaction, btcb::endpoint_key{ boost::asio::ip::address_v6::any ().to_bytes (), 55555 });
	}

	node->start ();
	system.deadline_set (10s);
	while (system.nodes.back ()->network.empty () || system.nodes.front ()->network.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	// Confirm that the peers match with the endpoints we are expecting
	ASSERT_EQ (1, system.nodes.front ()->network.size ());
	auto list1 (system.nodes[0]->network.list (2));
	ASSERT_EQ (system.nodes[1]->network.endpoint (), list1[0]->get_endpoint ());
	ASSERT_EQ (btcb::transport::transport_type::tcp, list1[0]->get_type ());
	ASSERT_EQ (1, node->network.size ());
	auto list2 (system.nodes[1]->network.list (2));
	ASSERT_EQ (system.nodes[0]->network.endpoint (), list2[0]->get_endpoint ());
	ASSERT_EQ (btcb::transport::transport_type::tcp, list2[0]->get_type ());
	// Stop the peer node and check that it is removed from the store
	system.nodes.front ()->stop ();

	system.deadline_set (10s);
	while (system.nodes.back ()->network.size () == 1)
	{
		ASSERT_NO_ERROR (system.poll ());
	}

	ASSERT_TRUE (system.nodes.back ()->network.empty ());

	// Uncontactable peer should not be stored
	auto transaction (store.tx_begin_read ());
	ASSERT_EQ (store.peer_count (transaction), 1);
	ASSERT_TRUE (store.peer_exists (transaction, endpoint_key));

	node->stop ();
}

TEST (node, peer_cache_restart)
{
	btcb::system system (24000, 1);
	ASSERT_TRUE (system.nodes[0]->network.empty ());
	auto endpoint = system.nodes[0]->network.endpoint ();
	btcb::endpoint_key endpoint_key{ endpoint.address ().to_v6 ().to_bytes (), endpoint.port () };
	auto path (btcb::unique_path ());
	{
		btcb::node_init init;
		auto node (std::make_shared<btcb::node> (init, system.io_ctx, 24001, path, system.alarm, system.logging, system.work));
		system.nodes.push_back (node);
		auto & store = node->store;
		{
			// Add a peer to the database
			auto transaction (store.tx_begin_write ());
			store.peer_put (transaction, endpoint_key);
		}
		node->start ();
		system.deadline_set (10s);
		while (node->network.empty ())
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		// Confirm that the peers match with the endpoints we are expecting
		auto list (node->network.list (2));
		ASSERT_EQ (system.nodes[0]->network.endpoint (), list[0]->get_endpoint ());
		ASSERT_EQ (1, node->network.size ());
		node->stop ();
	}
	// Restart node
	{
		btcb::node_init init;
		auto node (std::make_shared<btcb::node> (init, system.io_ctx, 24002, path, system.alarm, system.logging, system.work));
		system.nodes.push_back (node);
		// Check cached peers after restart
		node->start ();
		auto & store = node->store;
		{
			auto transaction (store.tx_begin_read ());
			ASSERT_EQ (store.peer_count (transaction), 1);
			ASSERT_TRUE (store.peer_exists (transaction, endpoint_key));
		}
		system.deadline_set (10s);
		while (node->network.empty ())
		{
			ASSERT_NO_ERROR (system.poll ());
		}
		// Confirm that the peers match with the endpoints we are expecting
		auto list (node->network.list (2));
		ASSERT_EQ (system.nodes[0]->network.endpoint (), list[0]->get_endpoint ());
		ASSERT_EQ (1, node->network.size ());
		node->stop ();
	}
}

TEST (node, unchecked_cleanup)
{
	btcb::system system (24000, 1);
	btcb::keypair key;
	auto & node (*system.nodes[0]);
	auto open (std::make_shared<btcb::state_block> (key.pub, 0, key.pub, 1, key.pub, key.prv, key.pub, system.work.generate (key.pub)));
	node.process_active (open);
	node.block_processor.flush ();
	node.config.unchecked_cutoff_time = std::chrono::seconds (2);
	{
		auto transaction (node.store.tx_begin_read ());
		auto unchecked_count (node.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
	}
	std::this_thread::sleep_for (std::chrono::seconds (1));
	node.unchecked_cleanup ();
	{
		auto transaction (node.store.tx_begin_read ());
		auto unchecked_count (node.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 1);
	}
	std::this_thread::sleep_for (std::chrono::seconds (2));
	node.unchecked_cleanup ();
	{
		auto transaction (node.store.tx_begin_read ());
		auto unchecked_count (node.store.unchecked_count (transaction));
		ASSERT_EQ (unchecked_count, 0);
	}
}

/** This checks that a  node can be opened (without being blocked) when a write lock is held elsewhere */
TEST (node, dont_write_lock_node)
{
	auto path = btcb::unique_path ();

	std::promise<void> write_lock_held_promise;
	std::promise<void> finished_promise;
	// clang-format off
	std::thread ([&path, &write_lock_held_promise, &finished_promise]() {
		btcb::logger_mt logger;
		bool init (false);
		btcb::mdb_store store (init, logger, path / "data.ldb");
		btcb::genesis genesis;
		{
			auto transaction (store.tx_begin_write ());
			store.initialize (transaction, genesis);
		}

		// Hold write lock open until main thread is done needing it
		auto transaction (store.tx_begin_write ());
		write_lock_held_promise.set_value ();
		finished_promise.get_future ().wait ();
	})
	.detach ();
	// clang-format off

	write_lock_held_promise.get_future ().wait ();

	// Check inactive node can finish executing while a write lock is open
	btcb::inactive_node node (path);
	finished_promise.set_value ();
}


TEST (active_difficulty, recalculate_work)
{
	btcb::system system;
	btcb::node_config node_config (24000, system.logging);
	node_config.enable_voting = false;
	auto & node1 = *system.add_node (node_config);
	btcb::genesis genesis;
	btcb::keypair key1;
	ASSERT_EQ (node1.network_params.network.publish_threshold, node1.active.active_difficulty ());
	auto send1 (std::make_shared<btcb::send_block> (genesis.hash (), key1.pub, 0, btcb::test_genesis_key.prv, btcb::test_genesis_key.pub, 0));
	node1.work_generate_blocking (*send1);
	uint64_t difficulty1;
	btcb::work_validate (*send1, &difficulty1);
	auto multiplier1 = btcb::difficulty::to_multiplier (difficulty1, node1.network_params.network.publish_threshold);
	// Process as local block
	node1.process_active (send1);
	system.deadline_set (2s);
	while (node1.active.empty ())
	{
		ASSERT_NO_ERROR (system.poll ());
	}
	auto sum (std::accumulate (node1.active.multipliers_cb.begin (), node1.active.multipliers_cb.end (), double(0)));
	ASSERT_EQ (node1.active.active_difficulty (), btcb::difficulty::from_multiplier (sum / node1.active.multipliers_cb.size (), node1.network_params.network.publish_threshold));
	std::unique_lock<std::mutex> lock (node1.active.mutex);
	// Fake history records to force work recalculation
	for (auto i (0); i < node1.active.multipliers_cb.size (); i++)
	{
		node1.active.multipliers_cb.push_back (multiplier1 * (1 + i / 100.));
	}
	node1.work_generate_blocking (*send1);
	uint64_t difficulty2;
	btcb::work_validate (*send1, &difficulty2);
	node1.process_active (send1);
	node1.active.update_active_difficulty (lock);
	lock.unlock ();
	sum = std::accumulate (node1.active.multipliers_cb.begin (), node1.active.multipliers_cb.end (), double(0));
	ASSERT_EQ (node1.active.active_difficulty (), btcb::difficulty::from_multiplier (sum / node1.active.multipliers_cb.size (), node1.network_params.network.publish_threshold));
}

namespace
{
void add_required_children_node_config_tree (btcb::jsonconfig & tree)
{
	btcb::logging logging1;
	btcb::jsonconfig logging_l;
	logging1.serialize_json (logging_l);
	tree.put_child ("logging", logging_l);
	btcb::jsonconfig preconfigured_peers_l;
	tree.put_child ("preconfigured_peers", preconfigured_peers_l);
	btcb::jsonconfig preconfigured_representatives_l;
	tree.put_child ("preconfigured_representatives", preconfigured_representatives_l);
	btcb::jsonconfig work_peers_l;
	tree.put_child ("work_peers", work_peers_l);
	tree.put ("version", std::to_string (btcb::node_config::json_version ()));
}
}
