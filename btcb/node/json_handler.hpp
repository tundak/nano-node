#pragma once

#include <boost/property_tree/ptree.hpp>
#include <functional>
#include <btcb/lib/numbers.hpp>
#include <btcb/node/wallet.hpp>
#include <btcb/rpc/rpc.hpp>
#include <string>

namespace btcb
{
class node;
class node_rpc_config;

class json_handler : public std::enable_shared_from_this<btcb::json_handler>
{
public:
	json_handler (btcb::node &, btcb::node_rpc_config const &, std::string const &, std::function<void(std::string const &)> const &, std::function<void()> stop_callback = []() {});
	void process_request (bool unsafe = false);
	void account_balance ();
	void account_block_count ();
	void account_count ();
	void account_create ();
	void account_get ();
	void account_history ();
	void account_info ();
	void account_key ();
	void account_list ();
	void account_move ();
	void account_remove ();
	void account_representative ();
	void account_representative_set ();
	void account_weight ();
	void accounts_balances ();
	void accounts_create ();
	void accounts_frontiers ();
	void accounts_pending ();
	void active_difficulty ();
	void available_supply ();
	void block_info ();
	void block_confirm ();
	void blocks ();
	void blocks_info ();
	void block_account ();
	void block_count ();
	void block_count_type ();
	void block_create ();
	void block_hash ();
	void bootstrap ();
	void bootstrap_any ();
	void bootstrap_lazy ();
	void bootstrap_status ();
	void chain (bool = false);
	void confirmation_active ();
	void confirmation_history ();
	void confirmation_info ();
	void confirmation_quorum ();
	void confirmation_height_currently_processing ();
	void database_txn_tracker ();
	void delegators ();
	void delegators_count ();
	void deterministic_key ();
	void frontiers ();
	void keepalive ();
	void key_create ();
	void key_expand ();
	void ledger ();
	void mbtcb_to_raw (btcb::uint128_t = btcb::Mbcb_ratio);
	void mbtcb_from_raw (btcb::uint128_t = btcb::Mbcb_ratio);
	void node_id ();
	void node_id_delete ();
	void password_change ();
	void password_enter ();
	void password_valid (bool = false);
	void payment_begin ();
	void payment_init ();
	void payment_end ();
	void payment_wait ();
	void peers ();
	void pending ();
	void pending_exists ();
	void process ();
	void receive ();
	void receive_minimum ();
	void receive_minimum_set ();
	void representatives ();
	void representatives_online ();
	void republish ();
	void search_pending ();
	void search_pending_all ();
	void send ();
	void sign ();
	void stats ();
	void stats_clear ();
	void stop ();
	void unchecked ();
	void unchecked_clear ();
	void unchecked_get ();
	void unchecked_keys ();
	void unopened ();
	void uptime ();
	void validate_account_number ();
	void version ();
	void wallet_add ();
	void wallet_add_watch ();
	void wallet_balances ();
	void wallet_change_seed ();
	void wallet_contains ();
	void wallet_create ();
	void wallet_destroy ();
	void wallet_export ();
	void wallet_frontiers ();
	void wallet_history ();
	void wallet_info ();
	void wallet_key_valid ();
	void wallet_ledger ();
	void wallet_lock ();
	void wallet_pending ();
	void wallet_representative ();
	void wallet_representative_set ();
	void wallet_republish ();
	void wallet_seed ();
	void wallet_work_get ();
	void work_cancel ();
	void work_generate ();
	void work_get ();
	void work_peer_add ();
	void work_peers ();
	void work_peers_clear ();
	void work_set ();
	void work_validate ();
	std::string body;
	btcb::node & node;
	boost::property_tree::ptree request;
	std::function<void(std::string const &)> response;
	void response_errors ();
	std::error_code ec;
	std::string action;
	boost::property_tree::ptree response_l;
	std::shared_ptr<btcb::wallet> wallet_impl ();
	bool wallet_locked_impl (btcb::transaction const &, std::shared_ptr<btcb::wallet>);
	bool wallet_account_impl (btcb::transaction const &, std::shared_ptr<btcb::wallet>, btcb::account const &);
	btcb::account account_impl (std::string = "");
	btcb::amount amount_impl ();
	std::shared_ptr<btcb::block> block_impl (bool = true);
	std::shared_ptr<btcb::block> block_json_impl (bool = true);
	btcb::block_hash hash_impl (std::string = "hash");
	btcb::amount threshold_optional_impl ();
	uint64_t work_optional_impl ();
	uint64_t count_impl ();
	uint64_t count_optional_impl (uint64_t = std::numeric_limits<uint64_t>::max ());
	uint64_t offset_optional_impl (uint64_t = 0);
	bool enable_sign_hash{ false };
	std::function<void()> stop_callback;
	btcb::node_rpc_config const & node_rpc_config;
};

class inprocess_rpc_handler final : public btcb::rpc_handler_interface
{
public:
	inprocess_rpc_handler (btcb::node & node_a, btcb::node_rpc_config const & node_rpc_config_a, std::function<void()> stop_callback_a = []() {}) :
	node (node_a),
	stop_callback (stop_callback_a),
	node_rpc_config (node_rpc_config_a)
	{
	}

	void process_request (std::string const &, std::string const & body_a, std::function<void(std::string const &)> response_a) override
	{
		// Note that if the rpc action is async, the shared_ptr<json_handler> lifetime will be extended by the action handler
		auto handler (std::make_shared<btcb::json_handler> (node, node_rpc_config, body_a, response_a, [this]() {
			this->stop_callback ();
			this->stop ();
		}));
		handler->process_request ();
	}

	void stop () override
	{
		if (rpc)
		{
			rpc->stop ();
		}
	}

	void rpc_instance (btcb::rpc & rpc_a) override
	{
		rpc = rpc_a;
	}

private:
	btcb::node & node;
	boost::optional<btcb::rpc &> rpc;
	std::function<void()> stop_callback;
	btcb::node_rpc_config const & node_rpc_config;
};
}
