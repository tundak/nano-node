#include <btcb/secure/common.hpp>

#include <btcb/crypto_lib/random_pool.hpp>
#include <btcb/lib/interface.h>
#include <btcb/lib/numbers.hpp>
#include <btcb/node/common.hpp>
#include <btcb/secure/blockstore.hpp>
#include <btcb/secure/versioning.hpp>

#include <boost/endian/conversion.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <queue>

#include <iostream>
#include <limits>
#include <btcb/core_test/testutil.hpp>
#include <btcb/lib/config.hpp>

#include <crypto/ed25519-donna/ed25519.h>

size_t constexpr btcb::send_block::size;
size_t constexpr btcb::receive_block::size;
size_t constexpr btcb::open_block::size;
size_t constexpr btcb::change_block::size;
size_t constexpr btcb::state_block::size;

btcb::btcb_networks btcb::network_constants::active_network = btcb::btcb_networks::ACTIVE_NETWORK;

namespace
{
char const * test_private_key_data = "34F0A37AAD20F4A260F0A5B3CB3D7FB50673212263E58A380BC10474BB039CE4";
char const * test_public_key_data = "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0"; // xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo
char const * beta_public_key_data = "A59A47CC4F593E75AE9AD653FDA9358E2F7898D9ACC8C60E80D0495CE20FBA9F"; // xrb_3betaz86ypbygpqbookmzpnmd5jhh4efmd8arr9a3n4bdmj1zgnzad7xpmfp
char const * live_public_key_data = "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA"; // xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3
char const * test_genesis_data = R"%%%({
	"type": "open",
	"source": "B0311EA55708D6A53C75CDBF88300259C6D018522FE3D4D0A242E431F9E8B6D0",
	"representative": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"account": "xrb_3e3j5tkog48pnny9dmfzj1r16pg8t1e76dz5tmac6iq689wyjfpiij4txtdo",
	"work": "7b42a00ee91d5810",
	"signature": "ECDA914373A2F0CA1296475BAEE40500A7F0A7AD72A5A80C81D7FAB7F6C802B2CC7DB50F5DD0FB25B2EF11761FA7344A158DD5A700B21BD47DE5BD0F63153A02"
	})%%%";

char const * beta_genesis_data = R"%%%({
	"type": "open",
	"source": "A59A47CC4F593E75AE9AD653FDA9358E2F7898D9ACC8C60E80D0495CE20FBA9F",
	"representative": "xrb_3betaz86ypbygpqbookmzpnmd5jhh4efmd8arr9a3n4bdmj1zgnzad7xpmfp",
	"account": "xrb_3betaz86ypbygpqbookmzpnmd5jhh4efmd8arr9a3n4bdmj1zgnzad7xpmfp",
	"work": "000000000f0aaeeb",
	"signature": "A726490E3325E4FA59C1C900D5B6EEBB15FE13D99F49D475B93F0AACC5635929A0614CF3892764A04D1C6732A0D716FFEB254D4154C6F544D11E6630F201450B"
	})%%%";

char const * live_genesis_data = R"%%%({
	"type": "open",
	"source": "E89208DD038FBB269987689621D52292AE9C35941A7484756ECCED92A65093BA",
	"representative": "xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3",
	"account": "xrb_3t6k35gi95xu6tergt6p69ck76ogmitsa8mnijtpxm9fkcm736xtoncuohr3",
	"work": "62f05417dd3fb691",
	"signature": "9F0C933C8ADE004D808EA1985FA746A7E95BA2A38F867640F53EC8F180BDFE9E2C1268DEAD7C2664F356E37ABA362BC58E46DBA03E523A7B5A19E4B6EB12BB02"
	})%%%";
}

btcb::network_params::network_params () :
network_params (network_constants::active_network)
{
}

btcb::network_params::network_params (btcb::btcb_networks network_a) :
network (network_a), ledger (network), voting (network), node (network), portmapping (network), bootstrap (network)
{
	unsigned constexpr kdf_full_work = 64 * 1024;
	unsigned constexpr kdf_test_work = 8;
	kdf_work = network.is_test_network () ? kdf_test_work : kdf_full_work;
	header_magic_number = network.is_test_network () ? std::array<uint8_t, 2>{ { 'R', 'A' } } : network.is_beta_network () ? std::array<uint8_t, 2>{ { 'R', 'B' } } : std::array<uint8_t, 2>{ { 'R', 'C' } };
}

btcb::ledger_constants::ledger_constants (btcb::network_constants & network_constants) :
ledger_constants (network_constants.network ())
{
}

btcb::ledger_constants::ledger_constants (btcb::btcb_networks network_a) :
zero_key ("0"),
test_genesis_key (test_private_key_data),
btcb_test_account (test_public_key_data),
btcb_beta_account (beta_public_key_data),
btcb_live_account (live_public_key_data),
btcb_test_genesis (test_genesis_data),
btcb_beta_genesis (beta_genesis_data),
btcb_live_genesis (live_genesis_data),
genesis_account (network_a == btcb::btcb_networks::btcb_test_network ? btcb_test_account : network_a == btcb::btcb_networks::btcb_beta_network ? btcb_beta_account : btcb_live_account),
genesis_block (network_a == btcb::btcb_networks::btcb_test_network ? btcb_test_genesis : network_a == btcb::btcb_networks::btcb_beta_network ? btcb_beta_genesis : btcb_live_genesis),
genesis_amount (std::numeric_limits<btcb::uint128_t>::max ()),
burn_account (0)
{
}

btcb::random_constants::random_constants ()
{
	btcb::random_pool::generate_block (not_an_account.bytes.data (), not_an_account.bytes.size ());
	btcb::random_pool::generate_block (random_128.bytes.data (), random_128.bytes.size ());
}

btcb::node_constants::node_constants (btcb::network_constants & network_constants)
{
	period = network_constants.is_test_network () ? std::chrono::seconds (1) : std::chrono::seconds (60);
	cutoff = period * 5;
	syn_cookie_cutoff = std::chrono::seconds (5);
	backup_interval = std::chrono::minutes (5);
	search_pending_interval = network_constants.is_test_network () ? std::chrono::seconds (1) : std::chrono::seconds (5 * 60);
	peer_interval = search_pending_interval;
	unchecked_cleaning_interval = std::chrono::hours (2);
	process_confirmed_interval = network_constants.is_test_network () ? std::chrono::milliseconds (50) : std::chrono::milliseconds (500);
	max_weight_samples = network_constants.is_live_network () ? 4032 : 864;
	weight_period = 5 * 60; // 5 minutes
}

btcb::voting_constants::voting_constants (btcb::network_constants & network_constants)
{
	max_cache = network_constants.is_test_network () ? 2 : 1000;
}

btcb::portmapping_constants::portmapping_constants (btcb::network_constants & network_constants)
{
	mapping_timeout = network_constants.is_test_network () ? 53 : 3593;
	check_timeout = network_constants.is_test_network () ? 17 : 53;
}

btcb::bootstrap_constants::bootstrap_constants (btcb::network_constants & network_constants)
{
	lazy_max_pull_blocks = network_constants.is_test_network () ? 2 : 512;
}

/* Convenience constants for core_test which is always on the test network */
namespace
{
btcb::ledger_constants test_constants (btcb::btcb_networks::btcb_test_network);
}

btcb::keypair const & btcb::zero_key (test_constants.zero_key);
btcb::keypair const & btcb::test_genesis_key (test_constants.test_genesis_key);
btcb::account const & btcb::btcb_test_account (test_constants.btcb_test_account);
std::string const & btcb::btcb_test_genesis (test_constants.btcb_test_genesis);
btcb::account const & btcb::genesis_account (test_constants.genesis_account);
std::string const & btcb::genesis_block (test_constants.genesis_block);
btcb::uint128_t const & btcb::genesis_amount (test_constants.genesis_amount);
btcb::account const & btcb::burn_account (test_constants.burn_account);

// Create a new random keypair
btcb::keypair::keypair ()
{
	random_pool::generate_block (prv.data.bytes.data (), prv.data.bytes.size ());
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a private key
btcb::keypair::keypair (btcb::raw_key && prv_a) :
prv (std::move (prv_a))
{
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Create a keypair given a hex string of the private key
btcb::keypair::keypair (std::string const & prv_a)
{
	auto error (prv.data.decode_hex (prv_a));
	assert (!error);
	ed25519_publickey (prv.data.bytes.data (), pub.bytes.data ());
}

// Serialize a block prefixed with an 8-bit typecode
void btcb::serialize_block (btcb::stream & stream_a, btcb::block const & block_a)
{
	write (stream_a, block_a.type ());
	block_a.serialize (stream_a);
}

btcb::account_info::account_info (btcb::block_hash const & head_a, btcb::block_hash const & rep_block_a, btcb::block_hash const & open_block_a, btcb::amount const & balance_a, uint64_t modified_a, uint64_t block_count_a, uint64_t confirmation_height_a, btcb::epoch epoch_a) :
head (head_a),
rep_block (rep_block_a),
open_block (open_block_a),
balance (balance_a),
modified (modified_a),
block_count (block_count_a),
confirmation_height (confirmation_height_a),
epoch (epoch_a)
{
}

bool btcb::account_info::deserialize (btcb::stream & stream_a)
{
	auto error (false);
	try
	{
		btcb::read (stream_a, head.bytes);
		btcb::read (stream_a, rep_block.bytes);
		btcb::read (stream_a, open_block.bytes);
		btcb::read (stream_a, balance.bytes);
		btcb::read (stream_a, modified);
		btcb::read (stream_a, block_count);
		btcb::read (stream_a, confirmation_height);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool btcb::account_info::operator== (btcb::account_info const & other_a) const
{
	return head == other_a.head && rep_block == other_a.rep_block && open_block == other_a.open_block && balance == other_a.balance && modified == other_a.modified && block_count == other_a.block_count && confirmation_height == other_a.confirmation_height && epoch == other_a.epoch;
}

bool btcb::account_info::operator!= (btcb::account_info const & other_a) const
{
	return !(*this == other_a);
}

size_t btcb::account_info::db_size () const
{
	assert (reinterpret_cast<const uint8_t *> (this) == reinterpret_cast<const uint8_t *> (&head));
	assert (reinterpret_cast<const uint8_t *> (&head) + sizeof (head) == reinterpret_cast<const uint8_t *> (&rep_block));
	assert (reinterpret_cast<const uint8_t *> (&rep_block) + sizeof (rep_block) == reinterpret_cast<const uint8_t *> (&open_block));
	assert (reinterpret_cast<const uint8_t *> (&open_block) + sizeof (open_block) == reinterpret_cast<const uint8_t *> (&balance));
	assert (reinterpret_cast<const uint8_t *> (&balance) + sizeof (balance) == reinterpret_cast<const uint8_t *> (&modified));
	assert (reinterpret_cast<const uint8_t *> (&modified) + sizeof (modified) == reinterpret_cast<const uint8_t *> (&block_count));
	assert (reinterpret_cast<const uint8_t *> (&block_count) + sizeof (block_count) == reinterpret_cast<const uint8_t *> (&confirmation_height));
	return sizeof (head) + sizeof (rep_block) + sizeof (open_block) + sizeof (balance) + sizeof (modified) + sizeof (block_count) + sizeof (confirmation_height);
}

size_t btcb::block_counts::sum () const
{
	return send + receive + open + change + state_v0 + state_v1;
}

btcb::pending_info::pending_info (btcb::account const & source_a, btcb::amount const & amount_a, btcb::epoch epoch_a) :
source (source_a),
amount (amount_a),
epoch (epoch_a)
{
}

bool btcb::pending_info::deserialize (btcb::stream & stream_a)
{
	auto error (false);
	try
	{
		btcb::read (stream_a, source.bytes);
		btcb::read (stream_a, amount.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool btcb::pending_info::operator== (btcb::pending_info const & other_a) const
{
	return source == other_a.source && amount == other_a.amount && epoch == other_a.epoch;
}

btcb::pending_key::pending_key (btcb::account const & account_a, btcb::block_hash const & hash_a) :
account (account_a),
hash (hash_a)
{
}

bool btcb::pending_key::deserialize (btcb::stream & stream_a)
{
	auto error (false);
	try
	{
		btcb::read (stream_a, account.bytes);
		btcb::read (stream_a, hash.bytes);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

bool btcb::pending_key::operator== (btcb::pending_key const & other_a) const
{
	return account == other_a.account && hash == other_a.hash;
}

btcb::block_hash btcb::pending_key::key () const
{
	return account;
}

btcb::unchecked_info::unchecked_info (std::shared_ptr<btcb::block> block_a, btcb::account const & account_a, uint64_t modified_a, btcb::signature_verification verified_a) :
block (block_a),
account (account_a),
modified (modified_a),
verified (verified_a)
{
}

void btcb::unchecked_info::serialize (btcb::stream & stream_a) const
{
	assert (block != nullptr);
	btcb::serialize_block (stream_a, *block);
	btcb::write (stream_a, account.bytes);
	btcb::write (stream_a, modified);
	btcb::write (stream_a, verified);
}

bool btcb::unchecked_info::deserialize (btcb::stream & stream_a)
{
	block = btcb::deserialize_block (stream_a);
	bool error (block == nullptr);
	if (!error)
	{
		try
		{
			btcb::read (stream_a, account.bytes);
			btcb::read (stream_a, modified);
			btcb::read (stream_a, verified);
		}
		catch (std::runtime_error const &)
		{
			error = true;
		}
	}
	return error;
}

btcb::endpoint_key::endpoint_key (const std::array<uint8_t, 16> & address_a, uint16_t port_a) :
address (address_a), network_port (boost::endian::native_to_big (port_a))
{
}

const std::array<uint8_t, 16> & btcb::endpoint_key::address_bytes () const
{
	return address;
}

uint16_t btcb::endpoint_key::port () const
{
	return boost::endian::big_to_native (network_port);
}

btcb::block_info::block_info (btcb::account const & account_a, btcb::amount const & balance_a) :
account (account_a),
balance (balance_a)
{
}

bool btcb::vote::operator== (btcb::vote const & other_a) const
{
	auto blocks_equal (true);
	if (blocks.size () != other_a.blocks.size ())
	{
		blocks_equal = false;
	}
	else
	{
		for (auto i (0); blocks_equal && i < blocks.size (); ++i)
		{
			auto block (blocks[i]);
			auto other_block (other_a.blocks[i]);
			if (block.which () != other_block.which ())
			{
				blocks_equal = false;
			}
			else if (block.which ())
			{
				if (boost::get<btcb::block_hash> (block) != boost::get<btcb::block_hash> (other_block))
				{
					blocks_equal = false;
				}
			}
			else
			{
				if (!(*boost::get<std::shared_ptr<btcb::block>> (block) == *boost::get<std::shared_ptr<btcb::block>> (other_block)))
				{
					blocks_equal = false;
				}
			}
		}
	}
	return sequence == other_a.sequence && blocks_equal && account == other_a.account && signature == other_a.signature;
}

bool btcb::vote::operator!= (btcb::vote const & other_a) const
{
	return !(*this == other_a);
}

void btcb::vote::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("account", account.to_account ());
	tree.put ("signature", signature.number ());
	tree.put ("sequence", std::to_string (sequence));
	boost::property_tree::ptree blocks_tree;
	for (auto block : blocks)
	{
		boost::property_tree::ptree entry;
		if (block.which ())
		{
			entry.put ("", boost::get<btcb::block_hash> (block).to_string ());
		}
		else
		{
			entry.put ("", boost::get<std::shared_ptr<btcb::block>> (block)->hash ().to_string ());
		}
		blocks_tree.push_back (std::make_pair ("", entry));
	}
	tree.add_child ("blocks", blocks_tree);
}

std::string btcb::vote::to_json () const
{
	std::stringstream stream;
	boost::property_tree::ptree tree;
	serialize_json (tree);
	boost::property_tree::write_json (stream, tree);
	return stream.str ();
}

btcb::vote::vote (btcb::vote const & other_a) :
sequence (other_a.sequence),
blocks (other_a.blocks),
account (other_a.account),
signature (other_a.signature)
{
}

btcb::vote::vote (bool & error_a, btcb::stream & stream_a, btcb::block_uniquer * uniquer_a)
{
	error_a = deserialize (stream_a, uniquer_a);
}

btcb::vote::vote (bool & error_a, btcb::stream & stream_a, btcb::block_type type_a, btcb::block_uniquer * uniquer_a)
{
	try
	{
		btcb::read (stream_a, account.bytes);
		btcb::read (stream_a, signature.bytes);
		btcb::read (stream_a, sequence);

		while (stream_a.in_avail () > 0)
		{
			if (type_a == btcb::block_type::not_a_block)
			{
				btcb::block_hash block_hash;
				btcb::read (stream_a, block_hash);
				blocks.push_back (block_hash);
			}
			else
			{
				std::shared_ptr<btcb::block> block (btcb::deserialize_block (stream_a, type_a, uniquer_a));
				if (block == nullptr)
				{
					throw std::runtime_error ("Block is null");
				}
				blocks.push_back (block);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}

	if (blocks.empty ())
	{
		error_a = true;
	}
}

btcb::vote::vote (btcb::account const & account_a, btcb::raw_key const & prv_a, uint64_t sequence_a, std::shared_ptr<btcb::block> block_a) :
sequence (sequence_a),
blocks (1, block_a),
account (account_a),
signature (btcb::sign_message (prv_a, account_a, hash ()))
{
}

btcb::vote::vote (btcb::account const & account_a, btcb::raw_key const & prv_a, uint64_t sequence_a, std::vector<btcb::block_hash> const & blocks_a) :
sequence (sequence_a),
account (account_a)
{
	assert (!blocks_a.empty ());
	assert (blocks_a.size () <= 12);
	blocks.reserve (blocks_a.size ());
	std::copy (blocks_a.cbegin (), blocks_a.cend (), std::back_inserter (blocks));
	signature = btcb::sign_message (prv_a, account_a, hash ());
}

std::string btcb::vote::hashes_string () const
{
	std::string result;
	for (auto hash : *this)
	{
		result += hash.to_string ();
		result += ", ";
	}
	return result;
}

const std::string btcb::vote::hash_prefix = "vote ";

btcb::uint256_union btcb::vote::hash () const
{
	btcb::uint256_union result;
	blake2b_state hash;
	blake2b_init (&hash, sizeof (result.bytes));
	if (blocks.size () > 1 || (!blocks.empty () && blocks.front ().which ()))
	{
		blake2b_update (&hash, hash_prefix.data (), hash_prefix.size ());
	}
	for (auto block_hash : *this)
	{
		blake2b_update (&hash, block_hash.bytes.data (), sizeof (block_hash.bytes));
	}
	union
	{
		uint64_t qword;
		std::array<uint8_t, 8> bytes;
	};
	qword = sequence;
	blake2b_update (&hash, bytes.data (), sizeof (bytes));
	blake2b_final (&hash, result.bytes.data (), sizeof (result.bytes));
	return result;
}

btcb::uint256_union btcb::vote::full_hash () const
{
	btcb::uint256_union result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ().bytes));
	blake2b_update (&state, account.bytes.data (), sizeof (account.bytes.data ()));
	blake2b_update (&state, signature.bytes.data (), sizeof (signature.bytes.data ()));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

void btcb::vote::serialize (btcb::stream & stream_a, btcb::block_type type) const
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	for (auto const & block : blocks)
	{
		if (block.which ())
		{
			assert (type == btcb::block_type::not_a_block);
			write (stream_a, boost::get<btcb::block_hash> (block));
		}
		else
		{
			if (type == btcb::block_type::not_a_block)
			{
				write (stream_a, boost::get<std::shared_ptr<btcb::block>> (block)->hash ());
			}
			else
			{
				boost::get<std::shared_ptr<btcb::block>> (block)->serialize (stream_a);
			}
		}
	}
}

void btcb::vote::serialize (btcb::stream & stream_a) const
{
	write (stream_a, account);
	write (stream_a, signature);
	write (stream_a, sequence);
	for (auto const & block : blocks)
	{
		if (block.which ())
		{
			write (stream_a, btcb::block_type::not_a_block);
			write (stream_a, boost::get<btcb::block_hash> (block));
		}
		else
		{
			btcb::serialize_block (stream_a, *boost::get<std::shared_ptr<btcb::block>> (block));
		}
	}
}

bool btcb::vote::deserialize (btcb::stream & stream_a, btcb::block_uniquer * uniquer_a)
{
	auto error (false);
	try
	{
		btcb::read (stream_a, account);
		btcb::read (stream_a, signature);
		btcb::read (stream_a, sequence);

		btcb::block_type type;

		while (true)
		{
			if (btcb::try_read (stream_a, type))
			{
				// Reached the end of the stream
				break;
			}

			if (type == btcb::block_type::not_a_block)
			{
				btcb::block_hash block_hash;
				btcb::read (stream_a, block_hash);
				blocks.push_back (block_hash);
			}
			else
			{
				std::shared_ptr<btcb::block> block (btcb::deserialize_block (stream_a, type, uniquer_a));
				if (block == nullptr)
				{
					throw std::runtime_error ("Block is empty");
				}

				blocks.push_back (block);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	if (blocks.empty ())
	{
		error = true;
	}

	return error;
}

bool btcb::vote::validate () const
{
	return btcb::validate_message (account, hash (), signature);
}

btcb::block_hash btcb::iterate_vote_blocks_as_hash::operator() (boost::variant<std::shared_ptr<btcb::block>, btcb::block_hash> const & item) const
{
	btcb::block_hash result;
	if (item.which ())
	{
		result = boost::get<btcb::block_hash> (item);
	}
	else
	{
		result = boost::get<std::shared_ptr<btcb::block>> (item)->hash ();
	}
	return result;
}

boost::transform_iterator<btcb::iterate_vote_blocks_as_hash, btcb::vote_blocks_vec_iter> btcb::vote::begin () const
{
	return boost::transform_iterator<btcb::iterate_vote_blocks_as_hash, btcb::vote_blocks_vec_iter> (blocks.begin (), btcb::iterate_vote_blocks_as_hash ());
}

boost::transform_iterator<btcb::iterate_vote_blocks_as_hash, btcb::vote_blocks_vec_iter> btcb::vote::end () const
{
	return boost::transform_iterator<btcb::iterate_vote_blocks_as_hash, btcb::vote_blocks_vec_iter> (blocks.end (), btcb::iterate_vote_blocks_as_hash ());
}

btcb::vote_uniquer::vote_uniquer (btcb::block_uniquer & uniquer_a) :
uniquer (uniquer_a)
{
}

std::shared_ptr<btcb::vote> btcb::vote_uniquer::unique (std::shared_ptr<btcb::vote> vote_a)
{
	auto result (vote_a);
	if (result != nullptr && !result->blocks.empty ())
	{
		if (!result->blocks.front ().which ())
		{
			result->blocks.front () = uniquer.unique (boost::get<std::shared_ptr<btcb::block>> (result->blocks.front ()));
		}
		btcb::uint256_union key (vote_a->full_hash ());
		std::lock_guard<std::mutex> lock (mutex);
		auto & existing (votes[key]);
		if (auto block_l = existing.lock ())
		{
			result = block_l;
		}
		else
		{
			existing = vote_a;
		}

		release_assert (std::numeric_limits<CryptoPP::word32>::max () > votes.size ());
		for (auto i (0); i < cleanup_count && !votes.empty (); ++i)
		{
			auto random_offset = btcb::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (votes.size () - 1));

			auto existing (std::next (votes.begin (), random_offset));
			if (existing == votes.end ())
			{
				existing = votes.begin ();
			}
			if (existing != votes.end ())
			{
				if (auto block_l = existing->second.lock ())
				{
					// Still live
				}
				else
				{
					votes.erase (existing);
				}
			}
		}
	}
	return result;
}

size_t btcb::vote_uniquer::size ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return votes.size ();
}

namespace btcb
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_uniquer & vote_uniquer, const std::string & name)
{
	auto count = vote_uniquer.size ();
	auto sizeof_element = sizeof (vote_uniquer::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "votes", count, sizeof_element }));
	return composite;
}
}

btcb::genesis::genesis ()
{
	static btcb::network_params network_params;
	boost::property_tree::ptree tree;
	std::stringstream istream (network_params.ledger.genesis_block);
	boost::property_tree::read_json (istream, tree);
	open = btcb::deserialize_block_json (tree);
	assert (open != nullptr);
}

btcb::block_hash btcb::genesis::hash () const
{
	return open->hash ();
}
