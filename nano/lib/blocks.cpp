#include <btcb/crypto_lib/random_pool.hpp>
#include <btcb/lib/blocks.hpp>
#include <btcb/lib/numbers.hpp>
#include <btcb/lib/utility.hpp>

#include <boost/endian/conversion.hpp>

/** Compare blocks, first by type, then content. This is an optimization over dynamic_cast, which is very slow on some platforms. */
namespace
{
template <typename T>
bool blocks_equal (T const & first, btcb::block const & second)
{
	static_assert (std::is_base_of<btcb::block, T>::value, "Input parameter is not a block type");
	return (first.type () == second.type ()) && (static_cast<T const &> (second)) == first;
}
}

std::string btcb::block::to_json () const
{
	std::string result;
	serialize_json (result);
	return result;
}

size_t btcb::block::size (btcb::block_type type_a)
{
	size_t result (0);
	switch (type_a)
	{
		case btcb::block_type::invalid:
		case btcb::block_type::not_a_block:
			assert (false);
			break;
		case btcb::block_type::send:
			result = btcb::send_block::size;
			break;
		case btcb::block_type::receive:
			result = btcb::receive_block::size;
			break;
		case btcb::block_type::change:
			result = btcb::change_block::size;
			break;
		case btcb::block_type::open:
			result = btcb::open_block::size;
			break;
		case btcb::block_type::state:
			result = btcb::state_block::size;
			break;
	}
	return result;
}

btcb::block_hash btcb::block::hash () const
{
	btcb::uint256_union result;
	blake2b_state hash_l;
	auto status (blake2b_init (&hash_l, sizeof (result.bytes)));
	assert (status == 0);
	hash (hash_l);
	status = blake2b_final (&hash_l, result.bytes.data (), sizeof (result.bytes));
	assert (status == 0);
	return result;
}

btcb::block_hash btcb::block::full_hash () const
{
	btcb::block_hash result;
	blake2b_state state;
	blake2b_init (&state, sizeof (result.bytes));
	blake2b_update (&state, hash ().bytes.data (), sizeof (hash ()));
	auto signature (block_signature ());
	blake2b_update (&state, signature.bytes.data (), sizeof (signature));
	auto work (block_work ());
	blake2b_update (&state, &work, sizeof (work));
	blake2b_final (&state, result.bytes.data (), sizeof (result.bytes));
	return result;
}

btcb::account btcb::block::representative () const
{
	return 0;
}

btcb::block_hash btcb::block::source () const
{
	return 0;
}

btcb::block_hash btcb::block::link () const
{
	return 0;
}

btcb::account btcb::block::account () const
{
	return 0;
}

btcb::qualified_root btcb::block::qualified_root () const
{
	return btcb::qualified_root (previous (), root ());
}

void btcb::send_block::visit (btcb::block_visitor & visitor_a) const
{
	visitor_a.send_block (*this);
}

void btcb::send_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t btcb::send_block::block_work () const
{
	return work;
}

void btcb::send_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

btcb::send_hashables::send_hashables (btcb::block_hash const & previous_a, btcb::account const & destination_a, btcb::amount const & balance_a) :
previous (previous_a),
destination (destination_a),
balance (balance_a)
{
}

btcb::send_hashables::send_hashables (bool & error_a, btcb::stream & stream_a)
{
	try
	{
		btcb::read (stream_a, previous.bytes);
		btcb::read (stream_a, destination.bytes);
		btcb::read (stream_a, balance.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

btcb::send_hashables::send_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = destination.decode_account (destination_l);
			if (!error_a)
			{
				error_a = balance.decode_hex (balance_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void btcb::send_hashables::hash (blake2b_state & hash_a) const
{
	auto status (blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes)));
	assert (status == 0);
	status = blake2b_update (&hash_a, destination.bytes.data (), sizeof (destination.bytes));
	assert (status == 0);
	status = blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	assert (status == 0);
}

void btcb::send_block::serialize (btcb::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.destination.bytes);
	write (stream_a, hashables.balance.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool btcb::send_block::deserialize (btcb::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.destination.bytes);
		read (stream_a, hashables.balance.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::exception const &)
	{
		error = true;
	}

	return error;
}

void btcb::send_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void btcb::send_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "send");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	tree.put ("destination", hashables.destination.to_account ());
	std::string balance;
	hashables.balance.encode_hex (balance);
	tree.put ("balance", balance);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", btcb::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool btcb::send_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "send");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto destination_l (tree_a.get<std::string> ("destination"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.destination.decode_account (destination_l);
			if (!error)
			{
				error = hashables.balance.decode_hex (balance_l);
				if (!error)
				{
					error = btcb::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

btcb::send_block::send_block (btcb::block_hash const & previous_a, btcb::account const & destination_a, btcb::amount const & balance_a, btcb::raw_key const & prv_a, btcb::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, destination_a, balance_a),
signature (btcb::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

btcb::send_block::send_block (bool & error_a, btcb::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			btcb::read (stream_a, signature.bytes);
			btcb::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

btcb::send_block::send_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = btcb::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

bool btcb::send_block::operator== (btcb::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool btcb::send_block::valid_predecessor (btcb::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case btcb::block_type::send:
		case btcb::block_type::receive:
		case btcb::block_type::open:
		case btcb::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

btcb::block_type btcb::send_block::type () const
{
	return btcb::block_type::send;
}

bool btcb::send_block::operator== (btcb::send_block const & other_a) const
{
	auto result (hashables.destination == other_a.hashables.destination && hashables.previous == other_a.hashables.previous && hashables.balance == other_a.hashables.balance && work == other_a.work && signature == other_a.signature);
	return result;
}

btcb::block_hash btcb::send_block::previous () const
{
	return hashables.previous;
}

btcb::block_hash btcb::send_block::root () const
{
	return hashables.previous;
}

btcb::signature btcb::send_block::block_signature () const
{
	return signature;
}

void btcb::send_block::signature_set (btcb::uint512_union const & signature_a)
{
	signature = signature_a;
}

btcb::open_hashables::open_hashables (btcb::block_hash const & source_a, btcb::account const & representative_a, btcb::account const & account_a) :
source (source_a),
representative (representative_a),
account (account_a)
{
}

btcb::open_hashables::open_hashables (bool & error_a, btcb::stream & stream_a)
{
	try
	{
		btcb::read (stream_a, source.bytes);
		btcb::read (stream_a, representative.bytes);
		btcb::read (stream_a, account.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

btcb::open_hashables::open_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		error_a = source.decode_hex (source_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
			if (!error_a)
			{
				error_a = account.decode_account (account_l);
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void btcb::open_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
}

btcb::open_block::open_block (btcb::block_hash const & source_a, btcb::account const & representative_a, btcb::account const & account_a, btcb::raw_key const & prv_a, btcb::public_key const & pub_a, uint64_t work_a) :
hashables (source_a, representative_a, account_a),
signature (btcb::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
	assert (!representative_a.is_zero ());
}

btcb::open_block::open_block (btcb::block_hash const & source_a, btcb::account const & representative_a, btcb::account const & account_a, std::nullptr_t) :
hashables (source_a, representative_a, account_a),
work (0)
{
	signature.clear ();
}

btcb::open_block::open_block (bool & error_a, btcb::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			btcb::read (stream_a, signature);
			btcb::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

btcb::open_block::open_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = btcb::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void btcb::open_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t btcb::open_block::block_work () const
{
	return work;
}

void btcb::open_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

btcb::block_hash btcb::open_block::previous () const
{
	btcb::block_hash result (0);
	return result;
}

btcb::account btcb::open_block::account () const
{
	return hashables.account;
}

void btcb::open_block::serialize (btcb::stream & stream_a) const
{
	write (stream_a, hashables.source);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.account);
	write (stream_a, signature);
	write (stream_a, work);
}

bool btcb::open_block::deserialize (btcb::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.source);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.account);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void btcb::open_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void btcb::open_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "open");
	tree.put ("source", hashables.source.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("account", hashables.account.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", btcb::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool btcb::open_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "open");
		auto source_l (tree_a.get<std::string> ("source"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto account_l (tree_a.get<std::string> ("account"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.source.decode_hex (source_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = hashables.account.decode_hex (account_l);
				if (!error)
				{
					error = btcb::from_string_hex (work_l, work);
					if (!error)
					{
						error = signature.decode_hex (signature_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void btcb::open_block::visit (btcb::block_visitor & visitor_a) const
{
	visitor_a.open_block (*this);
}

btcb::block_type btcb::open_block::type () const
{
	return btcb::block_type::open;
}

bool btcb::open_block::operator== (btcb::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool btcb::open_block::operator== (btcb::open_block const & other_a) const
{
	return hashables.source == other_a.hashables.source && hashables.representative == other_a.hashables.representative && hashables.account == other_a.hashables.account && work == other_a.work && signature == other_a.signature;
}

bool btcb::open_block::valid_predecessor (btcb::block const & block_a) const
{
	return false;
}

btcb::block_hash btcb::open_block::source () const
{
	return hashables.source;
}

btcb::block_hash btcb::open_block::root () const
{
	return hashables.account;
}

btcb::account btcb::open_block::representative () const
{
	return hashables.representative;
}

btcb::signature btcb::open_block::block_signature () const
{
	return signature;
}

void btcb::open_block::signature_set (btcb::uint512_union const & signature_a)
{
	signature = signature_a;
}

btcb::change_hashables::change_hashables (btcb::block_hash const & previous_a, btcb::account const & representative_a) :
previous (previous_a),
representative (representative_a)
{
}

btcb::change_hashables::change_hashables (bool & error_a, btcb::stream & stream_a)
{
	try
	{
		btcb::read (stream_a, previous);
		btcb::read (stream_a, representative);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

btcb::change_hashables::change_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = representative.decode_account (representative_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void btcb::change_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
}

btcb::change_block::change_block (btcb::block_hash const & previous_a, btcb::account const & representative_a, btcb::raw_key const & prv_a, btcb::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, representative_a),
signature (btcb::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

btcb::change_block::change_block (bool & error_a, btcb::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			btcb::read (stream_a, signature);
			btcb::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

btcb::change_block::change_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto work_l (tree_a.get<std::string> ("work"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			error_a = btcb::from_string_hex (work_l, work);
			if (!error_a)
			{
				error_a = signature.decode_hex (signature_l);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void btcb::change_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t btcb::change_block::block_work () const
{
	return work;
}

void btcb::change_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

btcb::block_hash btcb::change_block::previous () const
{
	return hashables.previous;
}

void btcb::change_block::serialize (btcb::stream & stream_a) const
{
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, signature);
	write (stream_a, work);
}

bool btcb::change_block::deserialize (btcb::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, signature);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void btcb::change_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void btcb::change_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "change");
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("work", btcb::to_string_hex (work));
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
}

bool btcb::change_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "change");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.representative.decode_hex (representative_l);
			if (!error)
			{
				error = btcb::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void btcb::change_block::visit (btcb::block_visitor & visitor_a) const
{
	visitor_a.change_block (*this);
}

btcb::block_type btcb::change_block::type () const
{
	return btcb::block_type::change;
}

bool btcb::change_block::operator== (btcb::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool btcb::change_block::operator== (btcb::change_block const & other_a) const
{
	return hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && work == other_a.work && signature == other_a.signature;
}

bool btcb::change_block::valid_predecessor (btcb::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case btcb::block_type::send:
		case btcb::block_type::receive:
		case btcb::block_type::open:
		case btcb::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

btcb::block_hash btcb::change_block::root () const
{
	return hashables.previous;
}

btcb::account btcb::change_block::representative () const
{
	return hashables.representative;
}

btcb::signature btcb::change_block::block_signature () const
{
	return signature;
}

void btcb::change_block::signature_set (btcb::uint512_union const & signature_a)
{
	signature = signature_a;
}

btcb::state_hashables::state_hashables (btcb::account const & account_a, btcb::block_hash const & previous_a, btcb::account const & representative_a, btcb::amount const & balance_a, btcb::uint256_union const & link_a) :
account (account_a),
previous (previous_a),
representative (representative_a),
balance (balance_a),
link (link_a)
{
}

btcb::state_hashables::state_hashables (bool & error_a, btcb::stream & stream_a)
{
	try
	{
		btcb::read (stream_a, account);
		btcb::read (stream_a, previous);
		btcb::read (stream_a, representative);
		btcb::read (stream_a, balance);
		btcb::read (stream_a, link);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

btcb::state_hashables::state_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		error_a = account.decode_account (account_l);
		if (!error_a)
		{
			error_a = previous.decode_hex (previous_l);
			if (!error_a)
			{
				error_a = representative.decode_account (representative_l);
				if (!error_a)
				{
					error_a = balance.decode_dec (balance_l);
					if (!error_a)
					{
						error_a = link.decode_account (link_l) && link.decode_hex (link_l);
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void btcb::state_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, account.bytes.data (), sizeof (account.bytes));
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, representative.bytes.data (), sizeof (representative.bytes));
	blake2b_update (&hash_a, balance.bytes.data (), sizeof (balance.bytes));
	blake2b_update (&hash_a, link.bytes.data (), sizeof (link.bytes));
}

btcb::state_block::state_block (btcb::account const & account_a, btcb::block_hash const & previous_a, btcb::account const & representative_a, btcb::amount const & balance_a, btcb::uint256_union const & link_a, btcb::raw_key const & prv_a, btcb::public_key const & pub_a, uint64_t work_a) :
hashables (account_a, previous_a, representative_a, balance_a, link_a),
signature (btcb::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

btcb::state_block::state_block (bool & error_a, btcb::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			btcb::read (stream_a, signature);
			btcb::read (stream_a, work);
			boost::endian::big_to_native_inplace (work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

btcb::state_block::state_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto type_l (tree_a.get<std::string> ("type"));
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = type_l != "state";
			if (!error_a)
			{
				error_a = btcb::from_string_hex (work_l, work);
				if (!error_a)
				{
					error_a = signature.decode_hex (signature_l);
				}
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void btcb::state_block::hash (blake2b_state & hash_a) const
{
	btcb::uint256_union preamble (static_cast<uint64_t> (btcb::block_type::state));
	blake2b_update (&hash_a, preamble.bytes.data (), preamble.bytes.size ());
	hashables.hash (hash_a);
}

uint64_t btcb::state_block::block_work () const
{
	return work;
}

void btcb::state_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

btcb::block_hash btcb::state_block::previous () const
{
	return hashables.previous;
}

btcb::account btcb::state_block::account () const
{
	return hashables.account;
}

void btcb::state_block::serialize (btcb::stream & stream_a) const
{
	write (stream_a, hashables.account);
	write (stream_a, hashables.previous);
	write (stream_a, hashables.representative);
	write (stream_a, hashables.balance);
	write (stream_a, hashables.link);
	write (stream_a, signature);
	write (stream_a, boost::endian::native_to_big (work));
}

bool btcb::state_block::deserialize (btcb::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.account);
		read (stream_a, hashables.previous);
		read (stream_a, hashables.representative);
		read (stream_a, hashables.balance);
		read (stream_a, hashables.link);
		read (stream_a, signature);
		read (stream_a, work);
		boost::endian::big_to_native_inplace (work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void btcb::state_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void btcb::state_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "state");
	tree.put ("account", hashables.account.to_account ());
	tree.put ("previous", hashables.previous.to_string ());
	tree.put ("representative", representative ().to_account ());
	tree.put ("balance", hashables.balance.to_string_dec ());
	tree.put ("link", hashables.link.to_string ());
	tree.put ("link_as_account", hashables.link.to_account ());
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("signature", signature_l);
	tree.put ("work", btcb::to_string_hex (work));
}

bool btcb::state_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "state");
		auto account_l (tree_a.get<std::string> ("account"));
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto representative_l (tree_a.get<std::string> ("representative"));
		auto balance_l (tree_a.get<std::string> ("balance"));
		auto link_l (tree_a.get<std::string> ("link"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.account.decode_account (account_l);
		if (!error)
		{
			error = hashables.previous.decode_hex (previous_l);
			if (!error)
			{
				error = hashables.representative.decode_account (representative_l);
				if (!error)
				{
					error = hashables.balance.decode_dec (balance_l);
					if (!error)
					{
						error = hashables.link.decode_account (link_l) && hashables.link.decode_hex (link_l);
						if (!error)
						{
							error = btcb::from_string_hex (work_l, work);
							if (!error)
							{
								error = signature.decode_hex (signature_l);
							}
						}
					}
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

void btcb::state_block::visit (btcb::block_visitor & visitor_a) const
{
	visitor_a.state_block (*this);
}

btcb::block_type btcb::state_block::type () const
{
	return btcb::block_type::state;
}

bool btcb::state_block::operator== (btcb::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool btcb::state_block::operator== (btcb::state_block const & other_a) const
{
	return hashables.account == other_a.hashables.account && hashables.previous == other_a.hashables.previous && hashables.representative == other_a.hashables.representative && hashables.balance == other_a.hashables.balance && hashables.link == other_a.hashables.link && signature == other_a.signature && work == other_a.work;
}

bool btcb::state_block::valid_predecessor (btcb::block const & block_a) const
{
	return true;
}

btcb::block_hash btcb::state_block::root () const
{
	return !hashables.previous.is_zero () ? hashables.previous : hashables.account;
}

btcb::block_hash btcb::state_block::link () const
{
	return hashables.link;
}

btcb::account btcb::state_block::representative () const
{
	return hashables.representative;
}

btcb::signature btcb::state_block::block_signature () const
{
	return signature;
}

void btcb::state_block::signature_set (btcb::uint512_union const & signature_a)
{
	signature = signature_a;
}

std::shared_ptr<btcb::block> btcb::deserialize_block_json (boost::property_tree::ptree const & tree_a, btcb::block_uniquer * uniquer_a)
{
	std::shared_ptr<btcb::block> result;
	try
	{
		auto type (tree_a.get<std::string> ("type"));
		if (type == "receive")
		{
			bool error (false);
			std::unique_ptr<btcb::receive_block> obj (new btcb::receive_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "send")
		{
			bool error (false);
			std::unique_ptr<btcb::send_block> obj (new btcb::send_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "open")
		{
			bool error (false);
			std::unique_ptr<btcb::open_block> obj (new btcb::open_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "change")
		{
			bool error (false);
			std::unique_ptr<btcb::change_block> obj (new btcb::change_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
		else if (type == "state")
		{
			bool error (false);
			std::unique_ptr<btcb::state_block> obj (new btcb::state_block (error, tree_a));
			if (!error)
			{
				result = std::move (obj);
			}
		}
	}
	catch (std::runtime_error const &)
	{
	}
	if (uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

std::shared_ptr<btcb::block> btcb::deserialize_block (btcb::stream & stream_a, btcb::block_uniquer * uniquer_a)
{
	btcb::block_type type;
	auto error (try_read (stream_a, type));
	std::shared_ptr<btcb::block> result;
	if (!error)
	{
		result = btcb::deserialize_block (stream_a, type);
	}
	return result;
}

std::shared_ptr<btcb::block> btcb::deserialize_block (btcb::stream & stream_a, btcb::block_type type_a, btcb::block_uniquer * uniquer_a)
{
	std::shared_ptr<btcb::block> result;
	switch (type_a)
	{
		case btcb::block_type::receive:
		{
			bool error (false);
			std::unique_ptr<btcb::receive_block> obj (new btcb::receive_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case btcb::block_type::send:
		{
			bool error (false);
			std::unique_ptr<btcb::send_block> obj (new btcb::send_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case btcb::block_type::open:
		{
			bool error (false);
			std::unique_ptr<btcb::open_block> obj (new btcb::open_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case btcb::block_type::change:
		{
			bool error (false);
			std::unique_ptr<btcb::change_block> obj (new btcb::change_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		case btcb::block_type::state:
		{
			bool error (false);
			std::unique_ptr<btcb::state_block> obj (new btcb::state_block (error, stream_a));
			if (!error)
			{
				result = std::move (obj);
			}
			break;
		}
		default:
			assert (false);
			break;
	}
	if (uniquer_a != nullptr)
	{
		result = uniquer_a->unique (result);
	}
	return result;
}

void btcb::receive_block::visit (btcb::block_visitor & visitor_a) const
{
	visitor_a.receive_block (*this);
}

bool btcb::receive_block::operator== (btcb::receive_block const & other_a) const
{
	auto result (hashables.previous == other_a.hashables.previous && hashables.source == other_a.hashables.source && work == other_a.work && signature == other_a.signature);
	return result;
}

void btcb::receive_block::serialize (btcb::stream & stream_a) const
{
	write (stream_a, hashables.previous.bytes);
	write (stream_a, hashables.source.bytes);
	write (stream_a, signature.bytes);
	write (stream_a, work);
}

bool btcb::receive_block::deserialize (btcb::stream & stream_a)
{
	auto error (false);
	try
	{
		read (stream_a, hashables.previous.bytes);
		read (stream_a, hashables.source.bytes);
		read (stream_a, signature.bytes);
		read (stream_a, work);
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}

	return error;
}

void btcb::receive_block::serialize_json (std::string & string_a) const
{
	boost::property_tree::ptree tree;
	serialize_json (tree);
	std::stringstream ostream;
	boost::property_tree::write_json (ostream, tree);
	string_a = ostream.str ();
}

void btcb::receive_block::serialize_json (boost::property_tree::ptree & tree) const
{
	tree.put ("type", "receive");
	std::string previous;
	hashables.previous.encode_hex (previous);
	tree.put ("previous", previous);
	std::string source;
	hashables.source.encode_hex (source);
	tree.put ("source", source);
	std::string signature_l;
	signature.encode_hex (signature_l);
	tree.put ("work", btcb::to_string_hex (work));
	tree.put ("signature", signature_l);
}

bool btcb::receive_block::deserialize_json (boost::property_tree::ptree const & tree_a)
{
	auto error (false);
	try
	{
		assert (tree_a.get<std::string> ("type") == "receive");
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		auto work_l (tree_a.get<std::string> ("work"));
		auto signature_l (tree_a.get<std::string> ("signature"));
		error = hashables.previous.decode_hex (previous_l);
		if (!error)
		{
			error = hashables.source.decode_hex (source_l);
			if (!error)
			{
				error = btcb::from_string_hex (work_l, work);
				if (!error)
				{
					error = signature.decode_hex (signature_l);
				}
			}
		}
	}
	catch (std::runtime_error const &)
	{
		error = true;
	}
	return error;
}

btcb::receive_block::receive_block (btcb::block_hash const & previous_a, btcb::block_hash const & source_a, btcb::raw_key const & prv_a, btcb::public_key const & pub_a, uint64_t work_a) :
hashables (previous_a, source_a),
signature (btcb::sign_message (prv_a, pub_a, hash ())),
work (work_a)
{
}

btcb::receive_block::receive_block (bool & error_a, btcb::stream & stream_a) :
hashables (error_a, stream_a)
{
	if (!error_a)
	{
		try
		{
			btcb::read (stream_a, signature);
			btcb::read (stream_a, work);
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

btcb::receive_block::receive_block (bool & error_a, boost::property_tree::ptree const & tree_a) :
hashables (error_a, tree_a)
{
	if (!error_a)
	{
		try
		{
			auto signature_l (tree_a.get<std::string> ("signature"));
			auto work_l (tree_a.get<std::string> ("work"));
			error_a = signature.decode_hex (signature_l);
			if (!error_a)
			{
				error_a = btcb::from_string_hex (work_l, work);
			}
		}
		catch (std::runtime_error const &)
		{
			error_a = true;
		}
	}
}

void btcb::receive_block::hash (blake2b_state & hash_a) const
{
	hashables.hash (hash_a);
}

uint64_t btcb::receive_block::block_work () const
{
	return work;
}

void btcb::receive_block::block_work_set (uint64_t work_a)
{
	work = work_a;
}

bool btcb::receive_block::operator== (btcb::block const & other_a) const
{
	return blocks_equal (*this, other_a);
}

bool btcb::receive_block::valid_predecessor (btcb::block const & block_a) const
{
	bool result;
	switch (block_a.type ())
	{
		case btcb::block_type::send:
		case btcb::block_type::receive:
		case btcb::block_type::open:
		case btcb::block_type::change:
			result = true;
			break;
		default:
			result = false;
			break;
	}
	return result;
}

btcb::block_hash btcb::receive_block::previous () const
{
	return hashables.previous;
}

btcb::block_hash btcb::receive_block::source () const
{
	return hashables.source;
}

btcb::block_hash btcb::receive_block::root () const
{
	return hashables.previous;
}

btcb::signature btcb::receive_block::block_signature () const
{
	return signature;
}

void btcb::receive_block::signature_set (btcb::uint512_union const & signature_a)
{
	signature = signature_a;
}

btcb::block_type btcb::receive_block::type () const
{
	return btcb::block_type::receive;
}

btcb::receive_hashables::receive_hashables (btcb::block_hash const & previous_a, btcb::block_hash const & source_a) :
previous (previous_a),
source (source_a)
{
}

btcb::receive_hashables::receive_hashables (bool & error_a, btcb::stream & stream_a)
{
	try
	{
		btcb::read (stream_a, previous.bytes);
		btcb::read (stream_a, source.bytes);
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

btcb::receive_hashables::receive_hashables (bool & error_a, boost::property_tree::ptree const & tree_a)
{
	try
	{
		auto previous_l (tree_a.get<std::string> ("previous"));
		auto source_l (tree_a.get<std::string> ("source"));
		error_a = previous.decode_hex (previous_l);
		if (!error_a)
		{
			error_a = source.decode_hex (source_l);
		}
	}
	catch (std::runtime_error const &)
	{
		error_a = true;
	}
}

void btcb::receive_hashables::hash (blake2b_state & hash_a) const
{
	blake2b_update (&hash_a, previous.bytes.data (), sizeof (previous.bytes));
	blake2b_update (&hash_a, source.bytes.data (), sizeof (source.bytes));
}

std::shared_ptr<btcb::block> btcb::block_uniquer::unique (std::shared_ptr<btcb::block> block_a)
{
	auto result (block_a);
	if (result != nullptr)
	{
		btcb::uint256_union key (block_a->full_hash ());
		std::lock_guard<std::mutex> lock (mutex);
		auto & existing (blocks[key]);
		if (auto block_l = existing.lock ())
		{
			result = block_l;
		}
		else
		{
			existing = block_a;
		}
		release_assert (std::numeric_limits<CryptoPP::word32>::max () > blocks.size ());
		for (auto i (0); i < cleanup_count && !blocks.empty (); ++i)
		{
			auto random_offset (btcb::random_pool::generate_word32 (0, static_cast<CryptoPP::word32> (blocks.size () - 1)));
			auto existing (std::next (blocks.begin (), random_offset));
			if (existing == blocks.end ())
			{
				existing = blocks.begin ();
			}
			if (existing != blocks.end ())
			{
				if (auto block_l = existing->second.lock ())
				{
					// Still live
				}
				else
				{
					blocks.erase (existing);
				}
			}
		}
	}
	return result;
}

size_t btcb::block_uniquer::size ()
{
	std::lock_guard<std::mutex> lock (mutex);
	return blocks.size ();
}

namespace btcb
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_uniquer & block_uniquer, const std::string & name)
{
	auto count = block_uniquer.size ();
	auto sizeof_element = sizeof (block_uniquer::value_type);
	auto composite = std::make_unique<seq_con_info_composite> (name);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "blocks", count, sizeof_element }));
	return composite;
}
}
