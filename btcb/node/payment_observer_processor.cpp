#include <btcb/node/payment_observer_processor.hpp>

#include <btcb/node/json_payment_observer.hpp>

btcb::payment_observer_processor::payment_observer_processor (btcb::node_observers::blocks_t & blocks)
{
	blocks.add ([this](std::shared_ptr<btcb::block> block_a, btcb::account const & account_a, btcb::uint128_t const &, bool) {
		observer_action (account_a);
	});
}

void btcb::payment_observer_processor::observer_action (btcb::account const & account_a)
{
	std::shared_ptr<btcb::json_payment_observer> observer;
	{
		std::lock_guard<std::mutex> lock (mutex);
		auto existing (payment_observers.find (account_a));
		if (existing != payment_observers.end ())
		{
			observer = existing->second;
		}
	}
	if (observer != nullptr)
	{
		observer->observe ();
	}
}

void btcb::payment_observer_processor::add (btcb::account const & account_a, std::shared_ptr<btcb::json_payment_observer> payment_observer_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	assert (payment_observers.find (account_a) == payment_observers.end ());
	payment_observers[account_a] = payment_observer_a;
}

void btcb::payment_observer_processor::erase (btcb::account & account_a)
{
	std::lock_guard<std::mutex> lock (mutex);
	assert (payment_observers.find (account_a) != payment_observers.end ());
	payment_observers.erase (account_a);
}
