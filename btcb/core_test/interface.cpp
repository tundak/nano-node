#include <gtest/gtest.h>

#include <memory>

#include <btcb/lib/blocks.hpp>
#include <btcb/lib/interface.h>
#include <btcb/lib/numbers.hpp>
#include <btcb/lib/work.hpp>

TEST (interface, bcb_uint128_to_dec)
{
	btcb::uint128_union zero (0);
	char text[40] = { 0 };
	bcb_uint128_to_dec (zero.bytes.data (), text);
	ASSERT_STREQ ("0", text);
}

TEST (interface, bcb_uint256_to_string)
{
	btcb::uint256_union zero (0);
	char text[65] = { 0 };
	bcb_uint256_to_string (zero.bytes.data (), text);
	ASSERT_STREQ ("0000000000000000000000000000000000000000000000000000000000000000", text);
}

TEST (interface, bcb_uint256_to_address)
{
	btcb::uint256_union zero (0);
	char text[66] = { 0 };
	bcb_uint256_to_address (zero.bytes.data (), text);

	/*
	 * Handle both "bcb_" and "btcb_" results, since it is not
	 * specified which is returned
	 */
	auto account_alpha = "1111111111111111111111111111111111111111111111111111hifc8npp";
	auto prefix = text[0] == 'x' ? "bcb" : "btcb";
	ASSERT_STREQ (boost::str (boost::format ("%1%_%2%") % prefix % account_alpha).c_str (), text);
}

TEST (interface, bcb_uint512_to_string)
{
	btcb::uint512_union zero (0);
	char text[129] = { 0 };
	bcb_uint512_to_string (zero.bytes.data (), text);
	ASSERT_STREQ ("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", text);
}

TEST (interface, bcb_uint128_from_dec)
{
	btcb::uint128_union zero (0);
	ASSERT_EQ (0, bcb_uint128_from_dec ("340282366920938463463374607431768211455", zero.bytes.data ()));
	ASSERT_EQ (1, bcb_uint128_from_dec ("340282366920938463463374607431768211456", zero.bytes.data ()));
	ASSERT_EQ (1, bcb_uint128_from_dec ("3402823669209384634633%4607431768211455", zero.bytes.data ()));
}

TEST (interface, bcb_uint256_from_string)
{
	btcb::uint256_union zero (0);
	ASSERT_EQ (0, bcb_uint256_from_string ("0000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, bcb_uint256_from_string ("00000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, bcb_uint256_from_string ("000000000000000000000000000%000000000000000000000000000000000000", zero.bytes.data ()));
}

TEST (interface, bcb_uint512_from_string)
{
	btcb::uint512_union zero (0);
	ASSERT_EQ (0, bcb_uint512_from_string ("00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, bcb_uint512_from_string ("000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
	ASSERT_EQ (1, bcb_uint512_from_string ("0000000000000000000000000000000000000000000000000000000000%000000000000000000000000000000000000000000000000000000000000000000000", zero.bytes.data ()));
}

TEST (interface, bcb_valid_address)
{
	ASSERT_EQ (0, bcb_valid_address ("bcb_1111111111111111111111111111111111111111111111111111hifc8npp"));
	ASSERT_EQ (1, bcb_valid_address ("bcb_1111111111111111111111111111111111111111111111111111hifc8nppp"));
	ASSERT_EQ (1, bcb_valid_address ("bcb_1111111211111111111111111111111111111111111111111111hifc8npp"));
	ASSERT_EQ (0, bcb_valid_address ("btcb_1111111111111111111111111111111111111111111111111111hifc8npp"));
	ASSERT_EQ (1, bcb_valid_address ("btcb_1111111111111111111111111111111111111111111111111111hifc8nppp"));
	ASSERT_EQ (1, bcb_valid_address ("btcb_1111111211111111111111111111111111111111111111111111hifc8npp"));
}

TEST (interface, bcb_seed_create)
{
	btcb::uint256_union seed;
	bcb_generate_random (seed.bytes.data ());
	ASSERT_FALSE (seed.is_zero ());
}

TEST (interface, bcb_seed_key)
{
	btcb::uint256_union seed (0);
	btcb::uint256_union prv;
	bcb_seed_key (seed.bytes.data (), 0, prv.bytes.data ());
	ASSERT_FALSE (prv.is_zero ());
}

TEST (interface, bcb_key_account)
{
	btcb::uint256_union prv (0);
	btcb::uint256_union pub;
	bcb_key_account (prv.bytes.data (), pub.bytes.data ());
	ASSERT_FALSE (pub.is_zero ());
}

TEST (interface, sign_transaction)
{
	btcb::raw_key key;
	bcb_generate_random (key.data.bytes.data ());
	btcb::uint256_union pub;
	bcb_key_account (key.data.bytes.data (), pub.bytes.data ());
	btcb::send_block send (0, 0, 0, key, pub, 0);
	ASSERT_FALSE (btcb::validate_message (pub, send.hash (), send.signature));
	send.signature.bytes[0] ^= 1;
	ASSERT_TRUE (btcb::validate_message (pub, send.hash (), send.signature));
	auto send_json (send.to_json ());
	auto transaction (bcb_sign_transaction (send_json.c_str (), key.data.bytes.data ()));
	boost::property_tree::ptree block_l;
	std::string transaction_l (transaction);
	std::stringstream block_stream (transaction_l);
	boost::property_tree::read_json (block_stream, block_l);
	auto block (btcb::deserialize_block_json (block_l));
	ASSERT_NE (nullptr, block);
	auto send1 (dynamic_cast<btcb::send_block *> (block.get ()));
	ASSERT_NE (nullptr, send1);
	ASSERT_FALSE (btcb::validate_message (pub, send.hash (), send1->signature));
	// Signatures should be non-deterministic
	auto transaction2 (bcb_sign_transaction (send_json.c_str (), key.data.bytes.data ()));
	ASSERT_NE (0, strcmp (transaction, transaction2));
	free (transaction);
	free (transaction2);
}

TEST (interface, fail_sign_transaction)
{
	btcb::uint256_union data (0);
	bcb_sign_transaction ("", data.bytes.data ());
}

TEST (interface, work_transaction)
{
	btcb::raw_key key;
	bcb_generate_random (key.data.bytes.data ());
	btcb::uint256_union pub;
	bcb_key_account (key.data.bytes.data (), pub.bytes.data ());
	btcb::send_block send (1, 0, 0, key, pub, 0);
	auto transaction (bcb_work_transaction (send.to_json ().c_str ()));
	boost::property_tree::ptree block_l;
	std::string transaction_l (transaction);
	std::stringstream block_stream (transaction_l);
	boost::property_tree::read_json (block_stream, block_l);
	auto block (btcb::deserialize_block_json (block_l));
	ASSERT_NE (nullptr, block);
	ASSERT_FALSE (btcb::work_validate (*block));
	free (transaction);
}

TEST (interface, fail_work_transaction)
{
	btcb::uint256_union data (0);
	bcb_work_transaction ("");
}
