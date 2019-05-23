#pragma once

#include <btcb/lib/config.hpp>
#include <btcb/secure/common.hpp>

namespace btcb
{
class block_store;
class stat;

class shared_ptr_block_hash final
{
public:
	size_t operator() (std::shared_ptr<btcb::block> const &) const;
	bool operator() (std::shared_ptr<btcb::block> const &, std::shared_ptr<btcb::block> const &) const;
};
using tally_t = std::map<btcb::uint128_t, std::shared_ptr<btcb::block>, std::greater<btcb::uint128_t>>;
class ledger final
{
public:
	ledger (btcb::block_store &, btcb::stat &, btcb::uint256_union const & = 1, btcb::account const & = 0);
	btcb::account account (btcb::transaction const &, btcb::block_hash const &) const;
	btcb::uint128_t amount (btcb::transaction const &, btcb::block_hash const &);
	btcb::uint128_t balance (btcb::transaction const &, btcb::block_hash const &) const;
	btcb::uint128_t account_balance (btcb::transaction const &, btcb::account const &);
	btcb::uint128_t account_pending (btcb::transaction const &, btcb::account const &);
	btcb::uint128_t weight (btcb::transaction const &, btcb::account const &);
	std::shared_ptr<btcb::block> successor (btcb::transaction const &, btcb::qualified_root const &);
	std::shared_ptr<btcb::block> forked_block (btcb::transaction const &, btcb::block const &);
	bool block_confirmed (btcb::transaction const & transaction_a, btcb::block_hash const & hash_a) const;
	btcb::block_hash latest (btcb::transaction const &, btcb::account const &);
	btcb::block_hash latest_root (btcb::transaction const &, btcb::account const &);
	btcb::block_hash representative (btcb::transaction const &, btcb::block_hash const &);
	btcb::block_hash representative_calculated (btcb::transaction const &, btcb::block_hash const &);
	bool block_exists (btcb::block_hash const &);
	bool block_exists (btcb::block_type, btcb::block_hash const &);
	std::string block_text (char const *);
	std::string block_text (btcb::block_hash const &);
	bool is_send (btcb::transaction const &, btcb::state_block const &) const;
	btcb::block_hash block_destination (btcb::transaction const &, btcb::block const &);
	btcb::block_hash block_source (btcb::transaction const &, btcb::block const &);
	btcb::process_return process (btcb::transaction const &, btcb::block const &, btcb::signature_verification = btcb::signature_verification::unknown);
	bool rollback (btcb::transaction const &, btcb::block_hash const &, std::vector<btcb::block_hash> &);
	bool rollback (btcb::transaction const &, btcb::block_hash const &);
	void change_latest (btcb::transaction const &, btcb::account const &, btcb::block_hash const &, btcb::account const &, btcb::uint128_union const &, uint64_t, bool = false, btcb::epoch = btcb::epoch::epoch_0);
	void dump_account_chain (btcb::account const &);
	bool could_fit (btcb::transaction const &, btcb::block const &);
	bool is_epoch_link (btcb::uint256_union const &);
	static btcb::uint128_t const unit;
	btcb::network_params network_params;
	btcb::block_store & store;
	btcb::stat & stats;
	std::unordered_map<btcb::account, btcb::uint128_t> bootstrap_weights;
	uint64_t bootstrap_weight_max_blocks{ 1 };
	std::atomic<bool> check_bootstrap_weights;
	btcb::uint256_union epoch_link;
	btcb::account epoch_signer;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (ledger & ledger, const std::string & name);
}
