#pragma once

#include <btcb/lib/blockbuilders.hpp>
#include <btcb/lib/blocks.hpp>
#include <btcb/lib/config.hpp>
#include <btcb/lib/numbers.hpp>
#include <btcb/lib/utility.hpp>
#include <btcb/secure/utility.hpp>

#include <boost/iterator/transform_iterator.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/variant.hpp>

#include <unordered_map>

#include <crypto/blake2/blake2.h>

namespace boost
{
template <>
struct hash<::btcb::uint256_union>
{
	size_t operator() (::btcb::uint256_union const & value_a) const
	{
		std::hash<::btcb::uint256_union> hash;
		return hash (value_a);
	}
};
template <>
struct hash<::btcb::uint512_union>
{
	size_t operator() (::btcb::uint512_union const & value_a) const
	{
		std::hash<::btcb::uint512_union> hash;
		return hash (value_a);
	}
};
}
namespace btcb
{
const uint8_t protocol_version = 0x11;
const uint8_t protocol_version_min = 0x0d;

/*
 * Do not bootstrap from nodes older than this version.
 * Also, on the beta network do not process messages from
 * nodes older than this version.
 */
const uint8_t protocol_version_reasonable_min = 0x0d;

/**
 * A key pair. The private key is generated from the random pool, or passed in
 * as a hex string. The public key is derived using ed25519.
 */
class keypair
{
public:
	keypair ();
	keypair (std::string const &);
	keypair (btcb::raw_key &&);
	btcb::public_key pub;
	btcb::raw_key prv;
};

/**
 * Tag for which epoch an entry belongs to
 */
enum class epoch : uint8_t
{
	invalid = 0,
	unspecified = 1,
	epoch_0 = 2,
	epoch_1 = 3
};

/**
 * Latest information about an account
 */
class account_info final
{
public:
	account_info () = default;
	account_info (btcb::block_hash const &, btcb::block_hash const &, btcb::block_hash const &, btcb::amount const &, uint64_t, uint64_t, uint64_t, epoch);
	bool deserialize (btcb::stream &);
	bool operator== (btcb::account_info const &) const;
	bool operator!= (btcb::account_info const &) const;
	size_t db_size () const;
	btcb::block_hash head{ 0 };
	btcb::block_hash rep_block{ 0 };
	btcb::block_hash open_block{ 0 };
	btcb::amount balance{ 0 };
	/** Seconds since posix epoch */
	uint64_t modified{ 0 };
	uint64_t block_count{ 0 };
	uint64_t confirmation_height{ 0 };
	btcb::epoch epoch{ btcb::epoch::epoch_0 };
};

/**
 * Information on an uncollected send
 */
class pending_info final
{
public:
	pending_info () = default;
	pending_info (btcb::account const &, btcb::amount const &, epoch);
	bool deserialize (btcb::stream &);
	bool operator== (btcb::pending_info const &) const;
	btcb::account source{ 0 };
	btcb::amount amount{ 0 };
	btcb::epoch epoch{ btcb::epoch::epoch_0 };
};
class pending_key final
{
public:
	pending_key () = default;
	pending_key (btcb::account const &, btcb::block_hash const &);
	bool deserialize (btcb::stream &);
	bool operator== (btcb::pending_key const &) const;
	btcb::block_hash key () const;
	btcb::account account{ 0 };
	btcb::block_hash hash{ 0 };
};

class endpoint_key final
{
public:
	endpoint_key () = default;

	/*
	 * @param address_a This should be in network byte order
	 * @param port_a This should be in host byte order
	 */
	endpoint_key (const std::array<uint8_t, 16> & address_a, uint16_t port_a);

	/*
	 * @return The ipv6 address in network byte order
	 */
	const std::array<uint8_t, 16> & address_bytes () const;

	/*
	 * @return The port in host byte order
	 */
	uint16_t port () const;

private:
	// Both stored internally in network byte order
	std::array<uint8_t, 16> address;
	uint16_t network_port{ 0 };
};

enum class no_value
{
	dummy
};

// Internally unchecked_key is equal to pending_key (2x uint256_union)
using unchecked_key = pending_key;

/**
 * Tag for block signature verification result
 */
enum class signature_verification : uint8_t
{
	unknown = 0,
	invalid = 1,
	valid = 2,
	valid_epoch = 3 // Valid for epoch blocks
};

/**
 * Information on an unchecked block
 */
class unchecked_info final
{
public:
	unchecked_info () = default;
	unchecked_info (std::shared_ptr<btcb::block>, btcb::account const &, uint64_t, btcb::signature_verification = btcb::signature_verification::unknown);
	void serialize (btcb::stream &) const;
	bool deserialize (btcb::stream &);
	std::shared_ptr<btcb::block> block;
	btcb::account account{ 0 };
	/** Seconds since posix epoch */
	uint64_t modified{ 0 };
	btcb::signature_verification verified{ btcb::signature_verification::unknown };
};

class block_info final
{
public:
	block_info () = default;
	block_info (btcb::account const &, btcb::amount const &);
	btcb::account account{ 0 };
	btcb::amount balance{ 0 };
};
class block_counts final
{
public:
	size_t sum () const;
	size_t send{ 0 };
	size_t receive{ 0 };
	size_t open{ 0 };
	size_t change{ 0 };
	size_t state_v0{ 0 };
	size_t state_v1{ 0 };
};
using vote_blocks_vec_iter = std::vector<boost::variant<std::shared_ptr<btcb::block>, btcb::block_hash>>::const_iterator;
class iterate_vote_blocks_as_hash final
{
public:
	iterate_vote_blocks_as_hash () = default;
	btcb::block_hash operator() (boost::variant<std::shared_ptr<btcb::block>, btcb::block_hash> const & item) const;
};
class vote final
{
public:
	vote () = default;
	vote (btcb::vote const &);
	vote (bool &, btcb::stream &, btcb::block_uniquer * = nullptr);
	vote (bool &, btcb::stream &, btcb::block_type, btcb::block_uniquer * = nullptr);
	vote (btcb::account const &, btcb::raw_key const &, uint64_t, std::shared_ptr<btcb::block>);
	vote (btcb::account const &, btcb::raw_key const &, uint64_t, std::vector<btcb::block_hash> const &);
	std::string hashes_string () const;
	btcb::uint256_union hash () const;
	btcb::uint256_union full_hash () const;
	bool operator== (btcb::vote const &) const;
	bool operator!= (btcb::vote const &) const;
	void serialize (btcb::stream &, btcb::block_type) const;
	void serialize (btcb::stream &) const;
	void serialize_json (boost::property_tree::ptree & tree) const;
	bool deserialize (btcb::stream &, btcb::block_uniquer * = nullptr);
	bool validate () const;
	boost::transform_iterator<btcb::iterate_vote_blocks_as_hash, btcb::vote_blocks_vec_iter> begin () const;
	boost::transform_iterator<btcb::iterate_vote_blocks_as_hash, btcb::vote_blocks_vec_iter> end () const;
	std::string to_json () const;
	// Vote round sequence number
	uint64_t sequence;
	// The blocks, or block hashes, that this vote is for
	std::vector<boost::variant<std::shared_ptr<btcb::block>, btcb::block_hash>> blocks;
	// Account that's voting
	btcb::account account;
	// Signature of sequence + block hashes
	btcb::signature signature;
	static const std::string hash_prefix;
};
/**
 * This class serves to find and return unique variants of a vote in order to minimize memory usage
 */
class vote_uniquer final
{
public:
	using value_type = std::pair<const btcb::uint256_union, std::weak_ptr<btcb::vote>>;

	vote_uniquer (btcb::block_uniquer &);
	std::shared_ptr<btcb::vote> unique (std::shared_ptr<btcb::vote>);
	size_t size ();

private:
	btcb::block_uniquer & uniquer;
	std::mutex mutex;
	std::unordered_map<std::remove_const_t<value_type::first_type>, value_type::second_type> votes;
	static unsigned constexpr cleanup_count = 2;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_uniquer & vote_uniquer, const std::string & name);

enum class vote_code
{
	invalid, // Vote is not signed correctly
	replay, // Vote does not have the highest sequence number, it's a replay
	vote // Vote has the highest sequence number
};

enum class process_result
{
	progress, // Hasn't been seen before, signed correctly
	bad_signature, // Signature was bad, forged or transmission error
	old, // Already seen and was valid
	negative_spend, // Malicious attempt to spend a negative amount
	fork, // Malicious fork based on previous
	unreceivable, // Source block doesn't exist, has already been received, or requires an account upgrade (epoch blocks)
	gap_previous, // Block marked as previous is unknown
	gap_source, // Block marked as source is unknown
	opened_burn_account, // The impossible happened, someone found the private key associated with the public key '0'.
	balance_mismatch, // Balance and amount delta don't match
	representative_mismatch, // Representative is changed when it is not allowed
	block_position // This block cannot follow the previous block
};
class process_return final
{
public:
	btcb::process_result code;
	btcb::account account;
	btcb::amount amount;
	btcb::account pending_account;
	boost::optional<bool> state_is_send;
	btcb::signature_verification verified;
};
enum class tally_result
{
	vote,
	changed,
	confirm
};

class genesis final
{
public:
	genesis ();
	btcb::block_hash hash () const;
	std::shared_ptr<btcb::block> open;
};

class network_params;

/** Genesis keys and ledger constants for network variants */
class ledger_constants
{
public:
	ledger_constants (btcb::network_constants & network_constants);
	ledger_constants (btcb::btcb_networks network_a);
	btcb::keypair zero_key;
	btcb::keypair test_genesis_key;
	btcb::account btcb_test_account;
	btcb::account btcb_beta_account;
	btcb::account btcb_live_account;
	std::string btcb_test_genesis;
	std::string btcb_beta_genesis;
	std::string btcb_live_genesis;
	btcb::account genesis_account;
	std::string genesis_block;
	btcb::uint128_t genesis_amount;
	btcb::account burn_account;
};

/** Constants which depend on random values (this class should never be used globally due to CryptoPP globals potentially not being initialized) */
class random_constants
{
public:
	random_constants ();
	btcb::account not_an_account;
	btcb::uint128_union random_128;
};

/** Node related constants whose value depends on the active network */
class node_constants
{
public:
	node_constants (btcb::network_constants & network_constants);
	std::chrono::seconds period;
	std::chrono::seconds cutoff;
	std::chrono::seconds syn_cookie_cutoff;
	std::chrono::minutes backup_interval;
	std::chrono::seconds search_pending_interval;
	std::chrono::seconds peer_interval;
	std::chrono::hours unchecked_cleaning_interval;
	std::chrono::milliseconds process_confirmed_interval;

	/** The maximum amount of samples for a 2 week period on live or 3 days on beta */
	uint64_t max_weight_samples;
	uint64_t weight_period;
};

/** Voting related constants whose value depends on the active network */
class voting_constants
{
public:
	voting_constants (btcb::network_constants & network_constants);
	size_t max_cache;
};

/** Port-mapping related constants whose value depends on the active network */
class portmapping_constants
{
public:
	portmapping_constants (btcb::network_constants & network_constants);
	// Timeouts are primes so they infrequently happen at the same time
	int mapping_timeout;
	int check_timeout;
};

/** Bootstrap related constants whose value depends on the active network */
class bootstrap_constants
{
public:
	bootstrap_constants (btcb::network_constants & network_constants);
	uint64_t lazy_max_pull_blocks;
};

/** Constants whose value depends on the active network */
class network_params
{
public:
	/** Populate values based on the current active network */
	network_params ();

	/** Populate values based on \p network_a */
	network_params (btcb::btcb_networks network_a);

	std::array<uint8_t, 2> header_magic_number;
	unsigned kdf_work;
	network_constants network;
	ledger_constants ledger;
	random_constants random;
	voting_constants voting;
	node_constants node;
	portmapping_constants portmapping;
	bootstrap_constants bootstrap;
};
}
