#pragma once

#include <btcb/lib/blocks.hpp>
#include <btcb/lib/numbers.hpp>
#include <btcb/lib/utility.hpp>
#include <btcb/node/transport/transport.hpp>
#include <btcb/secure/blockstore.hpp>

namespace btcb
{
class node_observers final
{
public:
	using blocks_t = btcb::observer_set<std::shared_ptr<btcb::block>, btcb::account const &, btcb::uint128_t const &, bool>;
	blocks_t blocks;
	btcb::observer_set<bool> wallet;
	btcb::observer_set<btcb::transaction const &, std::shared_ptr<btcb::vote>, std::shared_ptr<btcb::transport::channel>> vote;
	btcb::observer_set<btcb::account const &, bool> account_balance;
	btcb::observer_set<std::shared_ptr<btcb::transport::channel>> endpoint;
	btcb::observer_set<> disconnect;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (node_observers & node_observers, const std::string & name);
}
