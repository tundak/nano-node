#pragma once

#include <btcb/node/node_observers.hpp>
#include <btcb/node/wallet.hpp>
#include <string>
#include <vector>

namespace btcb
{
class node;

enum class payment_status
{
	not_a_status,
	unknown,
	nothing, // Timeout and nothing was received
	//insufficient, // Timeout and not enough was received
	//over, // More than requested received
	//success_fork, // Amount received but it involved a fork
	success // Amount received
};
class json_payment_observer final : public std::enable_shared_from_this<btcb::json_payment_observer>
{
public:
	json_payment_observer (btcb::node &, std::function<void(std::string const &)> const &, btcb::account const &, btcb::amount const &);
	void start (uint64_t);
	void observe ();
	void complete (btcb::payment_status);
	std::mutex mutex;
	std::condition_variable condition;
	btcb::node & node;
	btcb::account account;
	btcb::amount amount;
	std::function<void(std::string const &)> response;
	std::atomic_flag completed;
};
}
