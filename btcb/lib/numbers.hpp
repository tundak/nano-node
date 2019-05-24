#pragma once

#include <boost/multiprecision/cpp_int.hpp>

#include <crypto/cryptopp/osrng.h>

namespace btcb
{
using uint128_t = boost::multiprecision::uint128_t;
using uint256_t = boost::multiprecision::uint256_t;
using uint512_t = boost::multiprecision::uint512_t;
// SI dividers
btcb::uint128_t const Gbcb_ratio = btcb::uint128_t ("10000000000000000000000000000000000000"); // 10^37
btcb::uint128_t const Mbcb_ratio = btcb::uint128_t ("10000000000000000000000000000000000"); // 10^34
btcb::uint128_t const kbcb_ratio = btcb::uint128_t ("10000000000000000000000000000000"); // 10^31
btcb::uint128_t const bcb_ratio = btcb::uint128_t ("10000000000000000000000000000"); // 10^28
btcb::uint128_t const raw_ratio = btcb::uint128_t ("1"); // 10^0

union uint128_union final
{
public:
	uint128_union () = default;
	/**
	 * Decode from hex string
	 * @warning Aborts at runtime if the input is invalid
	 */
	uint128_union (std::string const &);
	uint128_union (uint64_t);
	uint128_union (btcb::uint128_t const &);
	bool operator== (btcb::uint128_union const &) const;
	bool operator!= (btcb::uint128_union const &) const;
	bool operator< (btcb::uint128_union const &) const;
	bool operator> (btcb::uint128_union const &) const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &, bool = false);
	bool decode_dec (std::string const &, btcb::uint128_t);
	std::string format_balance (btcb::uint128_t scale, int precision, bool group_digits);
	std::string format_balance (btcb::uint128_t scale, int precision, bool group_digits, const std::locale & locale);
	btcb::uint128_t number () const;
	void clear ();
	bool is_zero () const;
	std::string to_string () const;
	std::string to_string_dec () const;
	std::array<uint8_t, 16> bytes;
	std::array<char, 16> chars;
	std::array<uint32_t, 4> dwords;
	std::array<uint64_t, 2> qwords;
};
// Balances are 128 bit.
using amount = uint128_union;
class raw_key;
union uint256_union final
{
	uint256_union () = default;
	/**
	 * Decode from hex string
	 * @warning Aborts at runtime if the input is invalid
	 */
	uint256_union (std::string const &);
	uint256_union (uint64_t);
	uint256_union (btcb::uint256_t const &);
	void encrypt (btcb::raw_key const &, btcb::raw_key const &, uint128_union const &);
	uint256_union & operator^= (btcb::uint256_union const &);
	uint256_union operator^ (btcb::uint256_union const &) const;
	bool operator== (btcb::uint256_union const &) const;
	bool operator!= (btcb::uint256_union const &) const;
	bool operator< (btcb::uint256_union const &) const;
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	void encode_dec (std::string &) const;
	bool decode_dec (std::string const &);
	void encode_account (std::string &) const;
	std::string to_account () const;
	bool decode_account (std::string const &);
	std::array<uint8_t, 32> bytes;
	std::array<char, 32> chars;
	std::array<uint32_t, 8> dwords;
	std::array<uint64_t, 4> qwords;
	std::array<uint128_union, 2> owords;
	void clear ();
	bool is_zero () const;
	std::string to_string () const;
	btcb::uint256_t number () const;
};
// All keys and hashes are 256 bit.
using block_hash = uint256_union;
using account = uint256_union;
using public_key = uint256_union;
using private_key = uint256_union;
using secret_key = uint256_union;
class raw_key final
{
public:
	~raw_key ();
	void decrypt (btcb::uint256_union const &, btcb::raw_key const &, uint128_union const &);
	bool operator== (btcb::raw_key const &) const;
	bool operator!= (btcb::raw_key const &) const;
	btcb::uint256_union data;
};
union uint512_union final
{
	uint512_union () = default;
	uint512_union (btcb::uint256_union const &, btcb::uint256_union const &);
	uint512_union (btcb::uint512_t const &);
	bool operator== (btcb::uint512_union const &) const;
	bool operator!= (btcb::uint512_union const &) const;
	btcb::uint512_union & operator^= (btcb::uint512_union const &);
	void encode_hex (std::string &) const;
	bool decode_hex (std::string const &);
	std::array<uint8_t, 64> bytes;
	std::array<uint32_t, 16> dwords;
	std::array<uint64_t, 8> qwords;
	std::array<uint256_union, 2> uint256s;
	void clear ();
	bool is_zero () const;
	btcb::uint512_t number () const;
	std::string to_string () const;
};
using signature = uint512_union;
using qualified_root = uint512_union;

btcb::uint512_union sign_message (btcb::raw_key const &, btcb::public_key const &, btcb::uint256_union const &);
bool validate_message (btcb::public_key const &, btcb::uint256_union const &, btcb::uint512_union const &);
bool validate_message_batch (const unsigned char **, size_t *, const unsigned char **, const unsigned char **, size_t, int *);
void deterministic_key (btcb::uint256_union const &, uint32_t, btcb::uint256_union &);
btcb::public_key pub_key (btcb::private_key const &);

/* Conversion methods */
std::string to_string_hex (uint64_t const);
bool from_string_hex (std::string const &, uint64_t &);

/**
 * Convert a double to string in fixed format
 * @param precision_a (optional) use a specific precision (default is the maximum)
 */
std::string to_string (double const, int const precision_a = std::numeric_limits<double>::digits10);

namespace difficulty
{
	uint64_t from_multiplier (double const, uint64_t const);
	double to_multiplier (uint64_t const, uint64_t const);
}
}

namespace std
{
template <>
struct hash<::btcb::uint256_union>
{
	size_t operator() (::btcb::uint256_union const & data_a) const
	{
		return *reinterpret_cast<size_t const *> (data_a.bytes.data ());
	}
};
template <>
struct hash<::btcb::uint256_t>
{
	size_t operator() (::btcb::uint256_t const & number_a) const
	{
		return number_a.convert_to<size_t> ();
	}
};
template <>
struct hash<::btcb::uint512_union>
{
	size_t operator() (::btcb::uint512_union const & data_a) const
	{
		return *reinterpret_cast<size_t const *> (data_a.bytes.data ());
	}
};
}
