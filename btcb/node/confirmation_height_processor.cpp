#include <btcb/node/confirmation_height_processor.hpp>

#include <boost/optional.hpp>
#include <cassert>
#include <btcb/lib/logger_mt.hpp>
#include <btcb/lib/numbers.hpp>
#include <btcb/lib/utility.hpp>
#include <btcb/node/active_transactions.hpp>
#include <btcb/node/stats.hpp>
#include <btcb/secure/blockstore.hpp>
#include <btcb/secure/common.hpp>
#include <numeric>

btcb::confirmation_height_processor::confirmation_height_processor (btcb::pending_confirmation_height & pending_confirmation_height_a, btcb::block_store & store_a, btcb::stat & stats_a, btcb::active_transactions & active_a, btcb::block_hash const & epoch_link_a, btcb::logger_mt & logger_a) :
pending_confirmations (pending_confirmation_height_a),
store (store_a),
stats (stats_a),
active (active_a),
epoch_link (epoch_link_a),
logger (logger_a),
thread ([this]() {
	btcb::thread_role::set (btcb::thread_role::name::confirmation_height_processing);
	this->run ();
})
{
}

btcb::confirmation_height_processor::~confirmation_height_processor ()
{
	stop ();
}

void btcb::confirmation_height_processor::stop ()
{
	stopped = true;
	condition.notify_one ();
	if (thread.joinable ())
	{
		thread.join ();
	}
}

void btcb::confirmation_height_processor::run ()
{
	std::unique_lock<std::mutex> lk (pending_confirmations.mutex);
	while (!stopped)
	{
		if (!pending_confirmations.pending.empty ())
		{
			pending_confirmations.current_hash = *pending_confirmations.pending.begin ();
			pending_confirmations.pending.erase (pending_confirmations.current_hash);
			// Copy the hash so can be used outside owning the lock
			auto current_pending_block = pending_confirmations.current_hash;
			lk.unlock ();
			add_confirmation_height (current_pending_block);
			lk.lock ();
			pending_confirmations.current_hash = 0;
		}
		else
		{
			condition.wait (lk);
		}
	}
}

void btcb::confirmation_height_processor::add (btcb::block_hash const & hash_a)
{
	{
		std::lock_guard<std::mutex> lk (pending_confirmations.mutex);
		pending_confirmations.pending.insert (hash_a);
	}
	condition.notify_one ();
}

// This only check top-level blocks having their confirmation height sets, not anything below
bool btcb::confirmation_height_processor::is_processing_block (btcb::block_hash const & hash_a)
{
	return pending_confirmations.is_processing_block (hash_a);
}

/**
 * For all the blocks below this height which have been implicitly confirmed check if they
 * are open/receive blocks, and if so follow the source blocks and iteratively repeat to genesis.
 * To limit write locking and to keep the confirmation height ledger correctly synced, confirmations are
 * written from the ground upwards in batches.
 */
void btcb::confirmation_height_processor::add_confirmation_height (btcb::block_hash const & hash_a)
{
	boost::optional<conf_height_details> receive_details;
	auto current = hash_a;
	btcb::account_info account_info;
	std::deque<conf_height_details> pending_writes;
	assert (receive_source_pairs_size == 0);

	// Store the highest confirmation heights for accounts in pending_writes to reduce unnecessary iterating
	std::unordered_map<account, uint64_t> confirmation_height_pending_write_cache;

	release_assert (receive_source_pairs.empty ());
	auto error = false;

	auto read_transaction (store.tx_begin_read ());
	// Traverse account chain and all sources for receive blocks iteratively
	do
	{
		if (!receive_source_pairs.empty ())
		{
			receive_details = receive_source_pairs.back ().receive_details;
			current = receive_source_pairs.back ().source_hash;
		}
		else
		{
			// If receive_details is set then this is the final iteration and we are back to the original chain.
			// We need to confirm any blocks below the original hash (incl self) and the first receive block
			// (if the original block is not already a receive)
			if (receive_details)
			{
				current = hash_a;
				receive_details = boost::none;
			}
		}

		auto block_height (store.block_account_height (read_transaction, current));
		btcb::account account (store.block_account (read_transaction, current));
		release_assert (!store.account_get (read_transaction, account, account_info));
		auto confirmation_height = account_info.confirmation_height;

		auto account_it = confirmation_height_pending_write_cache.find (account);
		if (account_it != confirmation_height_pending_write_cache.cend () && account_it->second > confirmation_height)
		{
			confirmation_height = account_it->second;
		}

		auto count_before_receive = receive_source_pairs.size ();
		if (block_height > confirmation_height)
		{
			if ((block_height - confirmation_height) > 20000)
			{
				logger.always_log ("Iterating over a large account chain for setting confirmation height. The top block: ", current.to_string ());
			}

			collect_unconfirmed_receive_and_sources_for_account (block_height, confirmation_height, current, account, read_transaction);
		}

		// No longer need the read transaction
		read_transaction.reset ();

		// If this adds no more open or receive blocks, then we can now confirm this account as well as the linked open/receive block
		// Collect as pending any writes to the database and do them in bulk after a certain time.
		auto confirmed_receives_pending = (count_before_receive != receive_source_pairs.size ());
		if (!confirmed_receives_pending)
		{
			if (block_height > confirmation_height)
			{
				// Check whether the previous block has been seen. If so, the rest of sends below have already been seen so don't count them
				if (account_it != confirmation_height_pending_write_cache.cend ())
				{
					account_it->second = block_height;
				}
				else
				{
					confirmation_height_pending_write_cache.emplace (account, block_height);
				}

				pending_writes.emplace_back (account, current, block_height, block_height - confirmation_height);
			}

			if (receive_details)
			{
				// Check whether the previous block has been seen. If so, the rest of sends below have already been seen so don't count them
				auto const & receive_account = receive_details->account;
				auto receive_account_it = confirmation_height_pending_write_cache.find (receive_account);
				if (receive_account_it != confirmation_height_pending_write_cache.cend ())
				{
					// Get current height
					auto current_height = receive_account_it->second;
					receive_account_it->second = receive_details->height;
					receive_details->num_blocks_confirmed = receive_details->height - current_height;
				}
				else
				{
					confirmation_height_pending_write_cache.emplace (receive_account, receive_details->height);
				}

				pending_writes.push_back (*receive_details);
			}

			if (!receive_source_pairs.empty ())
			{
				// Pop from the end
				receive_source_pairs.erase (receive_source_pairs.end () - 1);
				--receive_source_pairs_size;
			}
		}

		// Check whether writing to the database should be done now
		auto total_pending_write_block_count = std::accumulate (pending_writes.cbegin (), pending_writes.cend (), uint64_t (0), [](uint64_t total, conf_height_details const & conf_height_details_a) {
			return total += conf_height_details_a.num_blocks_confirmed;
		});

		if ((pending_writes.size () >= batch_write_size || receive_source_pairs.empty ()) && !pending_writes.empty ())
		{
			error = write_pending (pending_writes, total_pending_write_block_count);
			// Don't set any more blocks as confirmed from the original hash if an inconsistency is found
			if (error)
			{
				receive_source_pairs.clear ();
				receive_source_pairs_size = 0;
				break;
			}
			assert (pending_writes.empty ());
		}
		// Exit early when the processor has been stopped, otherwise this function may take a
		// while (and hence keep the process running) if updating a long chain.
		if (stopped)
		{
			break;
		}

		read_transaction.renew ();
	} while (!receive_source_pairs.empty () || current != hash_a);
}

/*
 * Returns true if there was an error in finding one of the blocks to write a confirmation height for, false otherwise
 */
bool btcb::confirmation_height_processor::write_pending (std::deque<conf_height_details> & all_pending_a, int64_t total_pending_write_block_count_a)
{
	btcb::account_info account_info;
	auto total_pending_write_block_count (total_pending_write_block_count_a);

	// Write in batches
	while (total_pending_write_block_count > 0)
	{
		uint64_t num_accounts_processed = 0;
		auto transaction (store.tx_begin_write ());
		while (!all_pending_a.empty ())
		{
			const auto & pending = all_pending_a.front ();
			auto error = store.account_get (transaction, pending.account, account_info);
			release_assert (!error);
			if (pending.height > account_info.confirmation_height)
			{
#ifndef NDEBUG
				// Do more thorough checking in Debug mode, indicates programming error.
				btcb::block_sideband sideband;
				auto block = store.block_get (transaction, pending.hash, &sideband);
				assert (block != nullptr);
				assert (sideband.height == pending.height);
#else
				auto block = store.block_get (transaction, pending.hash);
#endif
				// Check that the block still exists as there may have been changes outside this processor.
				if (!block)
				{
					logger.always_log ("Failed to write confirmation height for: ", pending.hash.to_string ());
					stats.inc (btcb::stat::type::confirmation_height, btcb::stat::detail::invalid_block);
					return true;
				}

				stats.add (btcb::stat::type::confirmation_height, btcb::stat::detail::blocks_confirmed, btcb::stat::dir::in, pending.height - account_info.confirmation_height);
				assert (pending.num_blocks_confirmed == pending.height - account_info.confirmation_height);
				account_info.confirmation_height = pending.height;
				store.account_put (transaction, pending.account, account_info);
			}
			total_pending_write_block_count -= pending.num_blocks_confirmed;
			++num_accounts_processed;
			all_pending_a.erase (all_pending_a.begin ());

			if (num_accounts_processed >= batch_write_size)
			{
				// Commit changes periodically to reduce time holding write locks for long chains
				break;
			}
		}
	}
	return false;
}

void btcb::confirmation_height_processor::collect_unconfirmed_receive_and_sources_for_account (uint64_t block_height_a, uint64_t confirmation_height_a, btcb::block_hash const & hash_a, btcb::account const & account_a, btcb::read_transaction const & transaction_a)
{
	auto hash (hash_a);
	auto num_to_confirm = block_height_a - confirmation_height_a;

	// Store heights of blocks
	constexpr auto height_not_set = std::numeric_limits<uint64_t>::max ();
	auto next_height = height_not_set;
	while ((num_to_confirm > 0) && !hash.is_zero ())
	{
		active.confirm_block (hash);
		auto block (store.block_get (transaction_a, hash));
		if (block)
		{
			auto source (block->source ());
			if (source.is_zero ())
			{
				source = block->link ();
			}

			if (!source.is_zero () && source != epoch_link && store.source_exists (transaction_a, source))
			{
				auto block_height = confirmation_height_a + num_to_confirm;
				// Set the height for the receive block above (if there is one)
				if (next_height != height_not_set)
				{
					receive_source_pairs.back ().receive_details.num_blocks_confirmed = next_height - block_height;
				}

				receive_source_pairs.emplace_back (conf_height_details{ account_a, hash, block_height, height_not_set }, source);
				++receive_source_pairs_size;
				next_height = block_height;
			}

			hash = block->previous ();
		}

		// We could be traversing a very large account so we don't want to have open read transactions for too long.
		if (num_to_confirm % batch_read_size == 0)
		{
			transaction_a.refresh ();
		}

		--num_to_confirm;
	}

	// Update the number of blocks confirmed by the last receive block
	if (!receive_source_pairs.empty ())
	{
		receive_source_pairs.back ().receive_details.num_blocks_confirmed = receive_source_pairs.back ().receive_details.height - confirmation_height_a;
	}
}

namespace btcb
{
confirmation_height_processor::conf_height_details::conf_height_details (btcb::account const & account_a, btcb::block_hash const & hash_a, uint64_t height_a, uint64_t num_blocks_confirmed_a) :
account (account_a),
hash (hash_a),
height (height_a),
num_blocks_confirmed (num_blocks_confirmed_a)
{
}

confirmation_height_processor::receive_source_pair::receive_source_pair (confirmation_height_processor::conf_height_details const & receive_details_a, const block_hash & source_a) :
receive_details (receive_details_a),
source_hash (source_a)
{
}

std::unique_ptr<seq_con_info_component> collect_seq_con_info (confirmation_height_processor & confirmation_height_processor_a, const std::string & name_a)
{
	size_t receive_source_pairs_count = confirmation_height_processor_a.receive_source_pairs_size;
	auto composite = std::make_unique<seq_con_info_composite> (name_a);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "receive_source_pairs", receive_source_pairs_count, sizeof (decltype (confirmation_height_processor_a.receive_source_pairs)::value_type) }));
	return composite;
}
}

size_t btcb::pending_confirmation_height::size ()
{
	std::lock_guard<std::mutex> lk (mutex);
	return pending.size ();
}

bool btcb::pending_confirmation_height::is_processing_block (btcb::block_hash const & hash_a)
{
	// First check the hash currently being processed
	std::lock_guard<std::mutex> lk (mutex);
	if (!current_hash.is_zero () && current_hash == hash_a)
	{
		return true;
	}

	// Check remaining pending confirmations
	return pending.find (hash_a) != pending.cend ();
}

btcb::block_hash btcb::pending_confirmation_height::current ()
{
	std::lock_guard<std::mutex> lk (mutex);
	return current_hash;
}

namespace btcb
{
std::unique_ptr<seq_con_info_component> collect_seq_con_info (pending_confirmation_height & pending_confirmation_height_a, const std::string & name_a)
{
	size_t pending_count = pending_confirmation_height_a.size ();
	auto composite = std::make_unique<seq_con_info_composite> (name_a);
	composite->add_component (std::make_unique<seq_con_info_leaf> (seq_con_info{ "pending", pending_count, sizeof (btcb::block_hash) }));
	return composite;
}
}
