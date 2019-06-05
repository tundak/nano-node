#include "btcb/lib/errors.hpp"

std::string btcb::error_common_messages::message (int ev) const
{
	switch (static_cast<btcb::error_common> (ev))
	{
		case btcb::error_common::generic:
			return "Unknown error";
		case btcb::error_common::missing_account:
			return "Missing account";
		case btcb::error_common::missing_balance:
			return "Missing balance";
		case btcb::error_common::missing_link:
			return "Missing link, source or destination";
		case btcb::error_common::missing_previous:
			return "Missing previous";
		case btcb::error_common::missing_representative:
			return "Missing representative";
		case btcb::error_common::missing_signature:
			return "Missing signature";
		case btcb::error_common::missing_work:
			return "Missing work";
		case btcb::error_common::exception:
			return "Exception thrown";
		case btcb::error_common::account_exists:
			return "Account already exists";
		case btcb::error_common::account_not_found:
			return "Account not found";
		case btcb::error_common::account_not_found_wallet:
			return "Account not found in wallet";
		case btcb::error_common::bad_account_number:
			return "Bad account number";
		case btcb::error_common::bad_balance:
			return "Bad balance";
		case btcb::error_common::bad_link:
			return "Bad link value";
		case btcb::error_common::bad_previous:
			return "Bad previous hash";
		case btcb::error_common::bad_representative_number:
			return "Bad representative";
		case btcb::error_common::bad_source:
			return "Bad source";
		case btcb::error_common::bad_signature:
			return "Bad signature";
		case btcb::error_common::bad_private_key:
			return "Bad private key";
		case btcb::error_common::bad_public_key:
			return "Bad public key";
		case btcb::error_common::bad_seed:
			return "Bad seed";
		case btcb::error_common::bad_threshold:
			return "Bad threshold number";
		case btcb::error_common::bad_wallet_number:
			return "Bad wallet number";
		case btcb::error_common::bad_work_format:
			return "Bad work";
		case btcb::error_common::insufficient_balance:
			return "Insufficient balance";
		case btcb::error_common::invalid_amount:
			return "Invalid amount number";
		case btcb::error_common::invalid_amount_big:
			return "Amount too big";
		case btcb::error_common::invalid_count:
			return "Invalid count";
		case btcb::error_common::invalid_ip_address:
			return "Invalid IP address";
		case btcb::error_common::invalid_port:
			return "Invalid port";
		case btcb::error_common::invalid_index:
			return "Invalid index";
		case btcb::error_common::invalid_type_conversion:
			return "Invalid type conversion";
		case btcb::error_common::invalid_work:
			return "Invalid work";
		case btcb::error_common::numeric_conversion:
			return "Numeric conversion error";
		case btcb::error_common::tracking_not_enabled:
			return "Database transaction tracking is not enabled in the config";
		case btcb::error_common::wallet_lmdb_max_dbs:
			return "Failed to create wallet. Increase lmdb_max_dbs in node config";
		case btcb::error_common::wallet_locked:
			return "Wallet is locked";
		case btcb::error_common::wallet_not_found:
			return "Wallet not found";
	}

	return "Invalid error code";
}

std::string btcb::error_blocks_messages::message (int ev) const
{
	switch (static_cast<btcb::error_blocks> (ev))
	{
		case btcb::error_blocks::generic:
			return "Unknown error";
		case btcb::error_blocks::bad_hash_number:
			return "Bad hash number";
		case btcb::error_blocks::invalid_block:
			return "Block is invalid";
		case btcb::error_blocks::invalid_block_hash:
			return "Invalid block hash";
		case btcb::error_blocks::invalid_type:
			return "Invalid block type";
		case btcb::error_blocks::not_found:
			return "Block not found";
		case btcb::error_blocks::work_low:
			return "Block work is less than threshold";
	}

	return "Invalid error code";
}

std::string btcb::error_rpc_messages::message (int ev) const
{
	switch (static_cast<btcb::error_rpc> (ev))
	{
		case btcb::error_rpc::generic:
			return "Unknown error";
		case btcb::error_rpc::bad_destination:
			return "Bad destination account";
		case btcb::error_rpc::bad_difficulty_format:
			return "Bad difficulty";
		case btcb::error_rpc::bad_key:
			return "Bad key";
		case btcb::error_rpc::bad_link:
			return "Bad link number";
		case btcb::error_rpc::bad_previous:
			return "Bad previous";
		case btcb::error_rpc::bad_representative_number:
			return "Bad representative number";
		case btcb::error_rpc::bad_source:
			return "Bad source";
		case btcb::error_rpc::bad_timeout:
			return "Bad timeout number";
		case btcb::error_rpc::block_create_balance_mismatch:
			return "Balance mismatch for previous block";
		case btcb::error_rpc::block_create_key_required:
			return "Private key or local wallet and account required";
		case btcb::error_rpc::block_create_public_key_mismatch:
			return "Incorrect key for given account";
		case btcb::error_rpc::block_create_requirements_state:
			return "Previous, representative, final balance and link (source or destination) are required";
		case btcb::error_rpc::block_create_requirements_open:
			return "Representative account and source hash required";
		case btcb::error_rpc::block_create_requirements_receive:
			return "Previous hash and source hash required";
		case btcb::error_rpc::block_create_requirements_change:
			return "Representative account and previous hash required";
		case btcb::error_rpc::block_create_requirements_send:
			return "Destination account, previous hash, current balance and amount required";
		case btcb::error_rpc::confirmation_height_not_processing:
			return "There are no blocks currently being processed for adding confirmation height";
		case btcb::error_rpc::confirmation_not_found:
			return "Active confirmation not found";
		case btcb::error_rpc::difficulty_limit:
			return "Difficulty above config limit or below publish threshold";
		case btcb::error_rpc::invalid_balance:
			return "Invalid balance number";
		case btcb::error_rpc::invalid_destinations:
			return "Invalid destinations number";
		case btcb::error_rpc::invalid_offset:
			return "Invalid offset";
		case btcb::error_rpc::invalid_missing_type:
			return "Invalid or missing type argument";
		case btcb::error_rpc::invalid_root:
			return "Invalid root hash";
		case btcb::error_rpc::invalid_sources:
			return "Invalid sources number";
		case btcb::error_rpc::invalid_subtype:
			return "Invalid block subtype";
		case btcb::error_rpc::invalid_subtype_balance:
			return "Invalid block balance for given subtype";
		case btcb::error_rpc::invalid_subtype_epoch_link:
			return "Invalid epoch link";
		case btcb::error_rpc::invalid_subtype_previous:
			return "Invalid previous block for given subtype";
		case btcb::error_rpc::invalid_timestamp:
			return "Invalid timestamp";
		case btcb::error_rpc::payment_account_balance:
			return "Account has non-zero balance";
		case btcb::error_rpc::payment_unable_create_account:
			return "Unable to create transaction account";
		case btcb::error_rpc::rpc_control_disabled:
			return "RPC control is disabled";
		case btcb::error_rpc::sign_hash_disabled:
			return "Signing by block hash is disabled";
		case btcb::error_rpc::source_not_found:
			return "Source not found";
	}

	return "Invalid error code";
}

std::string btcb::error_process_messages::message (int ev) const
{
	switch (static_cast<btcb::error_process> (ev))
	{
		case btcb::error_process::generic:
			return "Unknown error";
		case btcb::error_process::bad_signature:
			return "Bad signature";
		case btcb::error_process::old:
			return "Old block";
		case btcb::error_process::negative_spend:
			return "Negative spend";
		case btcb::error_process::fork:
			return "Fork";
		case btcb::error_process::unreceivable:
			return "Unreceivable";
		case btcb::error_process::gap_previous:
			return "Gap previous block";
		case btcb::error_process::gap_source:
			return "Gap source block";
		case btcb::error_process::opened_burn_account:
			return "Burning account";
		case btcb::error_process::balance_mismatch:
			return "Balance and amount delta do not match";
		case btcb::error_process::block_position:
			return "This block cannot follow the previous block";
		case btcb::error_process::other:
			return "Error processing block";
	}

	return "Invalid error code";
}

std::string btcb::error_config_messages::message (int ev) const
{
	switch (static_cast<btcb::error_config> (ev))
	{
		case btcb::error_config::generic:
			return "Unknown error";
		case btcb::error_config::invalid_value:
			return "Invalid configuration value";
		case btcb::error_config::missing_value:
			return "Missing value in configuration";
	}

	return "Invalid error code";
}
