#pragma once

#include <btcb/lib/errors.hpp>
#include <btcb/lib/numbers.hpp>
#include <btcb/lib/utility.hpp>

#include <boost/property_tree/json_parser.hpp>
#include <cassert>
#include <crypto/blake2/blake2.h>
#include <streambuf>
#include <unordered_map>

namespace btcb
{
// We operate on streams of uint8_t by convention
using stream = std::basic_streambuf<uint8_t>;
// Read a raw byte stream the size of `T' and fill value.
template <typename T>
bool try_read (btcb::stream & stream_a, T & value)
{
	static_assert (std::is_standard_layout<T>::value, "Can't stream read non-standard layout types");
	auto amount_read (stream_a.sgetn (reinterpret_cast<uint8_t *> (&value), sizeof (value)));
	return amount_read != sizeof (value);
}
// A wrapper of try_read which throws if there is an error
template <typename T>
void read (btcb::stream & stream_a, T & value)
{
	auto error = try_read (stream_a, value);
	if (error)
	{
		throw std::runtime_error ("Failed to read type");
	}
}

template <typename T>
void write (btcb::stream & stream_a, T const & value)
{
	static_assert (std::is_standard_layout<T>::value, "Can't stream write non-standard layout types");
	auto amount_written (stream_a.sputn (reinterpret_cast<uint8_t const *> (&value), sizeof (value)));
	assert (amount_written == sizeof (value));
}
class block_visitor;
enum class block_type : uint8_t
{
	invalid = 0,
	not_a_block = 1,
	send = 2,
	receive = 3,
	open = 4,
	change = 5,
	state = 6
};
class block
{
public:
	// Return a digest of the hashables in this block.
	btcb::block_hash hash () const;
	// Return a digest of hashables and non-hashables in this block.
	btcb::block_hash full_hash () const;
	std::string to_json () const;
	virtual void hash (blake2b_state &) const = 0;
	virtual uint64_t block_work () const = 0;
	virtual void block_work_set (uint64_t) = 0;
	virtual btcb::account account () const;
	// Previous block in account's chain, zero for open block
	virtual btcb::block_hash previous () const = 0;
	// Source block for open/receive blocks, zero otherwise.
	virtual btcb::block_hash source () const;
	// Previous block or account number for open blocks
	virtual btcb::block_hash root () const = 0;
	// Qualified root value based on previous() and root()
	virtual btcb::qualified_root qualified_root () const;
	// Link field for state blocks, zero otherwise.
	virtual btcb::block_hash link () const;
	virtual btcb::account representative () const;
	virtual void serialize (btcb::stream &) const = 0;
	virtual void serialize_json (std::string &) const = 0;
	virtual void serialize_json (boost::property_tree::ptree &) const = 0;
	virtual void visit (btcb::block_visitor &) const = 0;
	virtual bool operator== (btcb::block const &) const = 0;
	virtual btcb::block_type type () const = 0;
	virtual btcb::signature block_signature () const = 0;
	virtual void signature_set (btcb::uint512_union const &) = 0;
	virtual ~block () = default;
	virtual bool valid_predecessor (btcb::block const &) const = 0;
	static size_t size (btcb::block_type);
};
class send_hashables
{
public:
	send_hashables () = default;
	send_hashables (btcb::account const &, btcb::block_hash const &, btcb::amount const &);
	send_hashables (bool &, btcb::stream &);
	send_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	btcb::block_hash previous;
	btcb::account destination;
	btcb::amount balance;
	static size_t constexpr size = sizeof (previous) + sizeof (destination) + sizeof (balance);
};
class send_block : public btcb::block
{
public:
	send_block () = default;
	send_block (btcb::block_hash const &, btcb::account const &, btcb::amount const &, btcb::raw_key const &, btcb::public_key const &, uint64_t);
	send_block (bool &, btcb::stream &);
	send_block (bool &, boost::property_tree::ptree const &);
	virtual ~send_block () = default;
	using btcb::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	btcb::block_hash previous () const override;
	btcb::block_hash root () const override;
	void serialize (btcb::stream &) const override;
	bool deserialize (btcb::stream &);
	void serialize_json (std::string &) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (btcb::block_visitor &) const override;
	btcb::block_type type () const override;
	btcb::signature block_signature () const override;
	void signature_set (btcb::uint512_union const &) override;
	bool operator== (btcb::block const &) const override;
	bool operator== (btcb::send_block const &) const;
	bool valid_predecessor (btcb::block const &) const override;
	send_hashables hashables;
	btcb::signature signature;
	uint64_t work;
	static size_t constexpr size = btcb::send_hashables::size + sizeof (signature) + sizeof (work);
};
class receive_hashables
{
public:
	receive_hashables () = default;
	receive_hashables (btcb::block_hash const &, btcb::block_hash const &);
	receive_hashables (bool &, btcb::stream &);
	receive_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	btcb::block_hash previous;
	btcb::block_hash source;
	static size_t constexpr size = sizeof (previous) + sizeof (source);
};
class receive_block : public btcb::block
{
public:
	receive_block () = default;
	receive_block (btcb::block_hash const &, btcb::block_hash const &, btcb::raw_key const &, btcb::public_key const &, uint64_t);
	receive_block (bool &, btcb::stream &);
	receive_block (bool &, boost::property_tree::ptree const &);
	virtual ~receive_block () = default;
	using btcb::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	btcb::block_hash previous () const override;
	btcb::block_hash source () const override;
	btcb::block_hash root () const override;
	void serialize (btcb::stream &) const override;
	bool deserialize (btcb::stream &);
	void serialize_json (std::string &) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (btcb::block_visitor &) const override;
	btcb::block_type type () const override;
	btcb::signature block_signature () const override;
	void signature_set (btcb::uint512_union const &) override;
	bool operator== (btcb::block const &) const override;
	bool operator== (btcb::receive_block const &) const;
	bool valid_predecessor (btcb::block const &) const override;
	receive_hashables hashables;
	btcb::signature signature;
	uint64_t work;
	static size_t constexpr size = btcb::receive_hashables::size + sizeof (signature) + sizeof (work);
};
class open_hashables
{
public:
	open_hashables () = default;
	open_hashables (btcb::block_hash const &, btcb::account const &, btcb::account const &);
	open_hashables (bool &, btcb::stream &);
	open_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	btcb::block_hash source;
	btcb::account representative;
	btcb::account account;
	static size_t constexpr size = sizeof (source) + sizeof (representative) + sizeof (account);
};
class open_block : public btcb::block
{
public:
	open_block () = default;
	open_block (btcb::block_hash const &, btcb::account const &, btcb::account const &, btcb::raw_key const &, btcb::public_key const &, uint64_t);
	open_block (btcb::block_hash const &, btcb::account const &, btcb::account const &, std::nullptr_t);
	open_block (bool &, btcb::stream &);
	open_block (bool &, boost::property_tree::ptree const &);
	virtual ~open_block () = default;
	using btcb::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	btcb::block_hash previous () const override;
	btcb::account account () const override;
	btcb::block_hash source () const override;
	btcb::block_hash root () const override;
	btcb::account representative () const override;
	void serialize (btcb::stream &) const override;
	bool deserialize (btcb::stream &);
	void serialize_json (std::string &) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (btcb::block_visitor &) const override;
	btcb::block_type type () const override;
	btcb::signature block_signature () const override;
	void signature_set (btcb::uint512_union const &) override;
	bool operator== (btcb::block const &) const override;
	bool operator== (btcb::open_block const &) const;
	bool valid_predecessor (btcb::block const &) const override;
	btcb::open_hashables hashables;
	btcb::signature signature;
	uint64_t work;
	static size_t constexpr size = btcb::open_hashables::size + sizeof (signature) + sizeof (work);
};
class change_hashables
{
public:
	change_hashables () = default;
	change_hashables (btcb::block_hash const &, btcb::account const &);
	change_hashables (bool &, btcb::stream &);
	change_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	btcb::block_hash previous;
	btcb::account representative;
	static size_t constexpr size = sizeof (previous) + sizeof (representative);
};
class change_block : public btcb::block
{
public:
	change_block () = default;
	change_block (btcb::block_hash const &, btcb::account const &, btcb::raw_key const &, btcb::public_key const &, uint64_t);
	change_block (bool &, btcb::stream &);
	change_block (bool &, boost::property_tree::ptree const &);
	virtual ~change_block () = default;
	using btcb::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	btcb::block_hash previous () const override;
	btcb::block_hash root () const override;
	btcb::account representative () const override;
	void serialize (btcb::stream &) const override;
	bool deserialize (btcb::stream &);
	void serialize_json (std::string &) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (btcb::block_visitor &) const override;
	btcb::block_type type () const override;
	btcb::signature block_signature () const override;
	void signature_set (btcb::uint512_union const &) override;
	bool operator== (btcb::block const &) const override;
	bool operator== (btcb::change_block const &) const;
	bool valid_predecessor (btcb::block const &) const override;
	btcb::change_hashables hashables;
	btcb::signature signature;
	uint64_t work;
	static size_t constexpr size = btcb::change_hashables::size + sizeof (signature) + sizeof (work);
};
class state_hashables
{
public:
	state_hashables () = default;
	state_hashables (btcb::account const &, btcb::block_hash const &, btcb::account const &, btcb::amount const &, btcb::uint256_union const &);
	state_hashables (bool &, btcb::stream &);
	state_hashables (bool &, boost::property_tree::ptree const &);
	void hash (blake2b_state &) const;
	// Account# / public key that operates this account
	// Uses:
	// Bulk signature validation in advance of further ledger processing
	// Arranging uncomitted transactions by account
	btcb::account account;
	// Previous transaction in this chain
	btcb::block_hash previous;
	// Representative of this account
	btcb::account representative;
	// Current balance of this account
	// Allows lookup of account balance simply by looking at the head block
	btcb::amount balance;
	// Link field contains source block_hash if receiving, destination account if sending
	btcb::uint256_union link;
	// Serialized size
	static size_t constexpr size = sizeof (account) + sizeof (previous) + sizeof (representative) + sizeof (balance) + sizeof (link);
};
class state_block : public btcb::block
{
public:
	state_block () = default;
	state_block (btcb::account const &, btcb::block_hash const &, btcb::account const &, btcb::amount const &, btcb::uint256_union const &, btcb::raw_key const &, btcb::public_key const &, uint64_t);
	state_block (bool &, btcb::stream &);
	state_block (bool &, boost::property_tree::ptree const &);
	virtual ~state_block () = default;
	using btcb::block::hash;
	void hash (blake2b_state &) const override;
	uint64_t block_work () const override;
	void block_work_set (uint64_t) override;
	btcb::block_hash previous () const override;
	btcb::account account () const override;
	btcb::block_hash root () const override;
	btcb::block_hash link () const override;
	btcb::account representative () const override;
	void serialize (btcb::stream &) const override;
	bool deserialize (btcb::stream &);
	void serialize_json (std::string &) const override;
	void serialize_json (boost::property_tree::ptree &) const override;
	bool deserialize_json (boost::property_tree::ptree const &);
	void visit (btcb::block_visitor &) const override;
	btcb::block_type type () const override;
	btcb::signature block_signature () const override;
	void signature_set (btcb::uint512_union const &) override;
	bool operator== (btcb::block const &) const override;
	bool operator== (btcb::state_block const &) const;
	bool valid_predecessor (btcb::block const &) const override;
	btcb::state_hashables hashables;
	btcb::signature signature;
	uint64_t work;
	static size_t constexpr size = btcb::state_hashables::size + sizeof (signature) + sizeof (work);
};
class block_visitor
{
public:
	virtual void send_block (btcb::send_block const &) = 0;
	virtual void receive_block (btcb::receive_block const &) = 0;
	virtual void open_block (btcb::open_block const &) = 0;
	virtual void change_block (btcb::change_block const &) = 0;
	virtual void state_block (btcb::state_block const &) = 0;
	virtual ~block_visitor () = default;
};
/**
 * This class serves to find and return unique variants of a block in order to minimize memory usage
 */
class block_uniquer
{
public:
	using value_type = std::pair<const btcb::uint256_union, std::weak_ptr<btcb::block>>;

	std::shared_ptr<btcb::block> unique (std::shared_ptr<btcb::block>);
	size_t size ();

private:
	std::mutex mutex;
	std::unordered_map<std::remove_const_t<value_type::first_type>, value_type::second_type> blocks;
	static unsigned constexpr cleanup_count = 2;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_uniquer & block_uniquer, const std::string & name);

std::shared_ptr<btcb::block> deserialize_block (btcb::stream &, btcb::block_uniquer * = nullptr);
std::shared_ptr<btcb::block> deserialize_block (btcb::stream &, btcb::block_type, btcb::block_uniquer * = nullptr);
std::shared_ptr<btcb::block> deserialize_block_json (boost::property_tree::ptree const &, btcb::block_uniquer * = nullptr);
void serialize_block (btcb::stream &, btcb::block const &);
}
