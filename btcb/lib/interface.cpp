#include <btcb/lib/interface.h>

#include <crypto/ed25519-donna/ed25519.h>

#include <boost/property_tree/json_parser.hpp>

#include <btcb/crypto_lib/random_pool.hpp>
#include <btcb/lib/blocks.hpp>
#include <btcb/lib/config.hpp>
#include <btcb/lib/numbers.hpp>
#include <btcb/lib/work.hpp>

#include <cstring>

extern "C" {
void bcb_uint128_to_dec (bcb_uint128 source, char * destination)
{
	auto const & number (*reinterpret_cast<btcb::uint128_union *> (source));
	strncpy (destination, number.to_string_dec ().c_str (), 40);
}

void bcb_uint256_to_string (bcb_uint256 source, char * destination)
{
	auto const & number (*reinterpret_cast<btcb::uint256_union *> (source));
	strncpy (destination, number.to_string ().c_str (), 65);
}

void bcb_uint256_to_address (bcb_uint256 source, char * destination)
{
	auto const & number (*reinterpret_cast<btcb::uint256_union *> (source));
	strncpy (destination, number.to_account ().c_str (), 65);
}

void bcb_uint512_to_string (bcb_uint512 source, char * destination)
{
	auto const & number (*reinterpret_cast<btcb::uint512_union *> (source));
	strncpy (destination, number.to_string ().c_str (), 129);
}

int bcb_uint128_from_dec (const char * source, bcb_uint128 destination)
{
	auto & number (*reinterpret_cast<btcb::uint128_union *> (destination));
	auto error (number.decode_dec (source));
	return error ? 1 : 0;
}

int bcb_uint256_from_string (const char * source, bcb_uint256 destination)
{
	auto & number (*reinterpret_cast<btcb::uint256_union *> (destination));
	auto error (number.decode_hex (source));
	return error ? 1 : 0;
}

int bcb_uint512_from_string (const char * source, bcb_uint512 destination)
{
	auto & number (*reinterpret_cast<btcb::uint512_union *> (destination));
	auto error (number.decode_hex (source));
	return error ? 1 : 0;
}

int bcb_valid_address (const char * account_a)
{
	btcb::uint256_union account;
	auto error (account.decode_account (account_a));
	return error ? 1 : 0;
}

void bcb_generate_random (bcb_uint256 seed)
{
	auto & number (*reinterpret_cast<btcb::uint256_union *> (seed));
	btcb::random_pool::generate_block (number.bytes.data (), number.bytes.size ());
}

void bcb_seed_key (bcb_uint256 seed, int index, bcb_uint256 destination)
{
	auto & seed_l (*reinterpret_cast<btcb::uint256_union *> (seed));
	auto & destination_l (*reinterpret_cast<btcb::uint256_union *> (destination));
	btcb::deterministic_key (seed_l, index, destination_l);
}

void bcb_key_account (const bcb_uint256 key, bcb_uint256 pub)
{
	ed25519_publickey (key, pub);
}

char * bcb_sign_transaction (const char * transaction, const bcb_uint256 private_key)
{
	char * result (nullptr);
	try
	{
		boost::property_tree::ptree block_l;
		std::string transaction_l (transaction);
		std::stringstream block_stream (transaction_l);
		boost::property_tree::read_json (block_stream, block_l);
		auto block (btcb::deserialize_block_json (block_l));
		if (block != nullptr)
		{
			btcb::uint256_union pub;
			ed25519_publickey (private_key, pub.bytes.data ());
			btcb::raw_key prv;
			prv.data = *reinterpret_cast<btcb::uint256_union *> (private_key);
			block->signature_set (btcb::sign_message (prv, pub, block->hash ()));
			auto json (block->to_json ());
			result = reinterpret_cast<char *> (malloc (json.size () + 1));
			strncpy (result, json.c_str (), json.size () + 1);
		}
	}
	catch (std::runtime_error const &)
	{
	}
	return result;
}

char * bcb_work_transaction (const char * transaction)
{
	static btcb::network_constants network_constants;
	char * result (nullptr);
	try
	{
		boost::property_tree::ptree block_l;
		std::string transaction_l (transaction);
		std::stringstream block_stream (transaction_l);
		boost::property_tree::read_json (block_stream, block_l);
		auto block (btcb::deserialize_block_json (block_l));
		if (block != nullptr)
		{
			btcb::work_pool pool (boost::thread::hardware_concurrency ());
			auto work (pool.generate (block->root (), network_constants.publish_threshold));
			block->block_work_set (work);
			auto json (block->to_json ());
			result = reinterpret_cast<char *> (malloc (json.size () + 1));
			strncpy (result, json.c_str (), json.size () + 1);
		}
	}
	catch (std::runtime_error const &)
	{
	}
	return result;
}
}
