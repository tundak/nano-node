#pragma once

#include <btcb/node/node_observers.hpp>

namespace btcb
{
class json_payment_observer;

class payment_observer_processor final
{
public:
	explicit payment_observer_processor (btcb::node_observers::blocks_t & blocks);
	void observer_action (btcb::account const & account_a);
	void add (btcb::account const & account_a, std::shared_ptr<btcb::json_payment_observer> payment_observer_a);
	void erase (btcb::account & account_a);

private:
	std::mutex mutex;
	std::unordered_map<btcb::account, std::shared_ptr<btcb::json_payment_observer>> payment_observers;
};
}
