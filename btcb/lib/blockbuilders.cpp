#include <crypto/cryptopp/osrng.h>
#include <btcb/lib/blockbuilders.hpp>
#include <unordered_map>

namespace
{
template <typename BLOCKTYPE>
void previous_hex_impl (std::string const & previous_hex, std::error_code & ec, BLOCKTYPE & block)
{
	btcb::block_hash previous;
	if (!previous.decode_hex (previous_hex))
	{
		block->hashables.previous = previous;
	}
	else
	{
		ec = btcb::error_common::bad_previous;
	}
}

template <typename BLOCKTYPE>
void account_hex_impl (std::string const & account_hex, std::error_code & ec, BLOCKTYPE & block)
{
	btcb::account account;
	if (!account.decode_hex (account_hex))
	{
		block->hashables.account = account;
	}
	else
	{
		ec = btcb::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void account_address_impl (std::string const & address, std::error_code & ec, BLOCKTYPE & block)
{
	btcb::account account;
	if (!account.decode_account (address))
	{
		block->hashables.account = account;
	}
	else
	{
		ec = btcb::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void representative_hex_impl (std::string const & account_hex, std::error_code & ec, BLOCKTYPE & block)
{
	btcb::account account;
	if (!account.decode_hex (account_hex))
	{
		block->hashables.representative = account;
	}
	else
	{
		ec = btcb::error_common::bad_representative_number;
	}
}

template <typename BLOCKTYPE>
void representative_address_impl (std::string const & address, std::error_code & ec, BLOCKTYPE & block)
{
	btcb::account account;
	if (!account.decode_account (address))
	{
		block->hashables.representative = account;
	}
	else
	{
		ec = btcb::error_common::bad_representative_number;
	}
}

template <typename BLOCKTYPE>
void destination_hex_impl (std::string const & account_hex, std::error_code & ec, BLOCKTYPE & block)
{
	btcb::account account;
	if (!account.decode_hex (account_hex))
	{
		block->hashables.destination = account;
	}
	else
	{
		ec = btcb::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void destination_address_impl (std::string const & address, std::error_code & ec, BLOCKTYPE & block)
{
	btcb::account account;
	if (!account.decode_account (address))
	{
		block->hashables.destination = account;
	}
	else
	{
		ec = btcb::error_common::bad_account_number;
	}
}

template <typename BLOCKTYPE>
void source_hex_impl (std::string const & source_hex, std::error_code & ec, BLOCKTYPE & block)
{
	btcb::block_hash source;
	if (!source.decode_hex (source_hex))
	{
		block->hashables.source = source;
	}
	else
	{
		ec = btcb::error_common::bad_source;
	}
}

template <typename BLOCKTYPE>
void balance_dec_impl (std::string const & balance_decimal, std::error_code & ec, BLOCKTYPE & block)
{
	btcb::amount balance;
	if (!balance.decode_dec (balance_decimal))
	{
		block->hashables.balance = balance;
	}
	else
	{
		ec = btcb::error_common::bad_balance;
	}
}

template <typename BLOCKTYPE>
void balance_hex_impl (std::string const & balance_hex, std::error_code & ec, BLOCKTYPE & block)
{
	btcb::amount balance;
	if (!balance.decode_hex (balance_hex))
	{
		block->hashables.balance = balance;
	}
	else
	{
		ec = btcb::error_common::bad_balance;
	}
}

/* The cost of looking up the error_code map is only taken if field-presence checks fail */
std::unordered_map<uint8_t, std::error_code> ec_map = {
	{ static_cast<uint8_t> (btcb::build_flags::account_present), btcb::error_common::missing_account },
	{ static_cast<uint8_t> (btcb::build_flags::balance_present), btcb::error_common::missing_balance },
	{ static_cast<uint8_t> (btcb::build_flags::link_present), btcb::error_common::missing_link },
	{ static_cast<uint8_t> (btcb::build_flags::previous_present), btcb::error_common::missing_previous },
	{ static_cast<uint8_t> (btcb::build_flags::representative_present), btcb::error_common::missing_representative },
	{ static_cast<uint8_t> (btcb::build_flags::signature_present), btcb::error_common::missing_signature },
	{ static_cast<uint8_t> (btcb::build_flags::work_present), btcb::error_common::missing_work }
};

/** Find first set bit as a mask, e.g. 10101000 => 0x08. Returns -1 if no bit is set. */
inline signed ffs_mask (uint8_t num)
{
	for (signed i = 0; i < 8; i++)
	{
		if ((num >> i) & 1)
		{
			return 1 << i;
		}
	}
	return -1;
}

/**
 * Check if \p build_state contains all the flags in \p block_all_flags.
 * If not, return the corresponding btcb::error_common::missing_<...> value.
 */
std::error_code check_fields_set (uint8_t block_all_flags, uint8_t build_state)
{
	std::error_code ec;

	// Figure out which fields are not set. Note that static typing ensures we cannot set values
	// not applicable to a given block type, we can only forget to set fields. This will be zero
	// if all fields are set and thus succeed.
	uint8_t res = block_all_flags ^ build_state;
	if (res)
	{
		// Convert the first bit set to a field mask and look up the error code.
		auto build_flags_mask = ffs_mask (res);
		assert (ec_map.find (build_flags_mask) != ec_map.end ());
		ec = ec_map[build_flags_mask];
	}
	return ec;
}
} // anonymous namespace

btcb::state_block_builder::state_block_builder ()
{
	make_block ();
}

btcb::state_block_builder & btcb::state_block_builder::make_block ()
{
	construct_block ();
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::from (btcb::state_block const & other_block)
{
	block->work = other_block.work;
	build_state |= build_flags::work_present;
	block->signature = other_block.signature;
	build_state |= build_flags::signature_present;
	block->hashables.account = other_block.hashables.account;
	build_state |= build_flags::account_present;
	block->hashables.balance = other_block.hashables.balance;
	build_state |= build_flags::balance_present;
	block->hashables.link = other_block.hashables.link;
	build_state |= build_flags::link_present;
	block->hashables.previous = other_block.hashables.previous;
	build_state |= build_flags::previous_present;
	block->hashables.representative = other_block.hashables.representative;
	build_state |= build_flags::representative_present;
	return *this;
}

void btcb::state_block_builder::validate ()
{
	if (!ec)
	{
		ec = check_fields_set (required_fields, build_state);
	}
}

btcb::state_block_builder & btcb::state_block_builder::zero ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.account.clear ();
	block->hashables.balance.clear ();
	block->hashables.link.clear ();
	block->hashables.previous.clear ();
	block->hashables.representative.clear ();
	build_state = required_fields;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::account (btcb::account account)
{
	block->hashables.account = account;
	build_state |= build_flags::account_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::account_hex (std::string account_hex)
{
	account_hex_impl (account_hex, ec, block);
	build_state |= build_flags::account_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::account_address (std::string address)
{
	account_address_impl (address, ec, block);
	build_state |= build_flags::account_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::representative (btcb::account account)
{
	block->hashables.representative = account;
	build_state |= build_flags::representative_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::representative_hex (std::string account_hex)
{
	representative_hex_impl (account_hex, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::representative_address (std::string address)
{
	representative_address_impl (address, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::previous (btcb::block_hash previous)
{
	block->hashables.previous = previous;
	build_state |= build_flags::previous_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	build_state |= build_flags::previous_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::balance (btcb::amount balance)
{
	block->hashables.balance = balance;
	build_state |= build_flags::balance_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::balance_dec (std::string balance_decimal)
{
	balance_dec_impl (balance_decimal, ec, block);
	build_state |= build_flags::balance_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::balance_hex (std::string balance_hex)
{
	balance_hex_impl (balance_hex, ec, block);
	build_state |= build_flags::balance_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::link (btcb::uint256_union link)
{
	block->hashables.link = link;
	build_state |= build_flags::link_present;
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::link_hex (std::string link_hex)
{
	btcb::uint256_union link;
	if (!link.decode_hex (link_hex))
	{
		block->hashables.link = link;
		build_state |= build_flags::link_present;
	}
	else
	{
		ec = btcb::error_common::bad_link;
	}
	return *this;
}

btcb::state_block_builder & btcb::state_block_builder::link_address (std::string link_address)
{
	btcb::account link;
	if (!link.decode_account (link_address))
	{
		block->hashables.link = link;
		build_state |= build_flags::link_present;
	}
	else
	{
		ec = btcb::error_common::bad_link;
	}
	return *this;
}

btcb::open_block_builder::open_block_builder ()
{
	make_block ();
}

btcb::open_block_builder & btcb::open_block_builder::make_block ()
{
	construct_block ();
	return *this;
}

void btcb::open_block_builder::validate ()
{
	if (!ec)
	{
		ec = check_fields_set (required_fields, build_state);
	}
}

btcb::open_block_builder & btcb::open_block_builder::zero ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.account.clear ();
	block->hashables.representative.clear ();
	block->hashables.source.clear ();
	build_state = required_fields;
	return *this;
}

btcb::open_block_builder & btcb::open_block_builder::account (btcb::account account)
{
	block->hashables.account = account;
	build_state |= build_flags::account_present;
	return *this;
}

btcb::open_block_builder & btcb::open_block_builder::account_hex (std::string account_hex)
{
	account_hex_impl (account_hex, ec, block);
	build_state |= build_flags::account_present;
	return *this;
}

btcb::open_block_builder & btcb::open_block_builder::account_address (std::string address)
{
	account_address_impl (address, ec, block);
	build_state |= build_flags::account_present;
	return *this;
}

btcb::open_block_builder & btcb::open_block_builder::representative (btcb::account account)
{
	block->hashables.representative = account;
	build_state |= build_flags::representative_present;
	return *this;
}

btcb::open_block_builder & btcb::open_block_builder::representative_hex (std::string account_hex)
{
	representative_hex_impl (account_hex, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

btcb::open_block_builder & btcb::open_block_builder::representative_address (std::string address)
{
	representative_address_impl (address, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

btcb::open_block_builder & btcb::open_block_builder::source (btcb::block_hash source)
{
	block->hashables.source = source;
	build_state |= build_flags::link_present;
	return *this;
}

btcb::open_block_builder & btcb::open_block_builder::source_hex (std::string source_hex)
{
	source_hex_impl (source_hex, ec, block);
	build_state |= build_flags::link_present;
	return *this;
}

btcb::change_block_builder::change_block_builder ()
{
	make_block ();
}

btcb::change_block_builder & btcb::change_block_builder::make_block ()
{
	construct_block ();
	return *this;
}

void btcb::change_block_builder::validate ()
{
	if (!ec)
	{
		ec = check_fields_set (required_fields, build_state);
	}
}

btcb::change_block_builder & btcb::change_block_builder::zero ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.previous.clear ();
	block->hashables.representative.clear ();
	build_state = required_fields;
	return *this;
}

btcb::change_block_builder & btcb::change_block_builder::representative (btcb::account account)
{
	block->hashables.representative = account;
	build_state |= build_flags::representative_present;
	return *this;
}

btcb::change_block_builder & btcb::change_block_builder::representative_hex (std::string account_hex)
{
	representative_hex_impl (account_hex, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

btcb::change_block_builder & btcb::change_block_builder::representative_address (std::string address)
{
	representative_address_impl (address, ec, block);
	build_state |= build_flags::representative_present;
	return *this;
}

btcb::change_block_builder & btcb::change_block_builder::previous (btcb::block_hash previous)
{
	block->hashables.previous = previous;
	build_state |= build_flags::previous_present;
	return *this;
}

btcb::change_block_builder & btcb::change_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	build_state |= build_flags::previous_present;
	return *this;
}

btcb::send_block_builder::send_block_builder ()
{
	make_block ();
}

btcb::send_block_builder & btcb::send_block_builder::make_block ()
{
	construct_block ();
	return *this;
}

void btcb::send_block_builder::validate ()
{
	if (!ec)
	{
		ec = check_fields_set (required_fields, build_state);
	}
}

btcb::send_block_builder & btcb::send_block_builder::zero ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.previous.clear ();
	block->hashables.destination.clear ();
	block->hashables.balance.clear ();
	build_state = required_fields;
	return *this;
}

btcb::send_block_builder & btcb::send_block_builder::destination (btcb::account account)
{
	block->hashables.destination = account;
	build_state |= build_flags::link_present;
	return *this;
}

btcb::send_block_builder & btcb::send_block_builder::destination_hex (std::string account_hex)
{
	destination_hex_impl (account_hex, ec, block);
	build_state |= build_flags::link_present;
	return *this;
}

btcb::send_block_builder & btcb::send_block_builder::destination_address (std::string address)
{
	destination_address_impl (address, ec, block);
	build_state |= build_flags::link_present;
	return *this;
}

btcb::send_block_builder & btcb::send_block_builder::previous (btcb::block_hash previous)
{
	block->hashables.previous = previous;
	build_state |= build_flags::previous_present;
	return *this;
}

btcb::send_block_builder & btcb::send_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	build_state |= build_flags::previous_present;
	return *this;
}

btcb::send_block_builder & btcb::send_block_builder::balance (btcb::amount balance)
{
	block->hashables.balance = balance;
	build_state |= build_flags::balance_present;
	return *this;
}

btcb::send_block_builder & btcb::send_block_builder::balance_dec (std::string balance_decimal)
{
	balance_dec_impl (balance_decimal, ec, block);
	build_state |= build_flags::balance_present;
	return *this;
}

btcb::send_block_builder & btcb::send_block_builder::balance_hex (std::string balance_hex)
{
	balance_hex_impl (balance_hex, ec, block);
	build_state |= build_flags::balance_present;
	return *this;
}

btcb::receive_block_builder::receive_block_builder ()
{
	make_block ();
}

btcb::receive_block_builder & btcb::receive_block_builder::make_block ()
{
	construct_block ();
	return *this;
}

void btcb::receive_block_builder::validate ()
{
	if (!ec)
	{
		ec = check_fields_set (required_fields, build_state);
	}
}

btcb::receive_block_builder & btcb::receive_block_builder::zero ()
{
	block->work = uint64_t (0);
	block->signature.clear ();
	block->hashables.previous.clear ();
	block->hashables.source.clear ();
	build_state = required_fields;
	return *this;
}

btcb::receive_block_builder & btcb::receive_block_builder::previous (btcb::block_hash previous)
{
	block->hashables.previous = previous;
	build_state |= build_flags::previous_present;
	return *this;
}

btcb::receive_block_builder & btcb::receive_block_builder::previous_hex (std::string previous_hex)
{
	previous_hex_impl (previous_hex, ec, block);
	build_state |= build_flags::previous_present;
	return *this;
}

btcb::receive_block_builder & btcb::receive_block_builder::source (btcb::block_hash source)
{
	block->hashables.source = source;
	build_state |= build_flags::link_present;
	return *this;
}

btcb::receive_block_builder & btcb::receive_block_builder::source_hex (std::string source_hex)
{
	source_hex_impl (source_hex, ec, block);
	build_state |= build_flags::link_present;
	return *this;
}
