#pragma once

#include <boost/asio.hpp>
#include <btcb/lib/logger_mt.hpp>
#include <btcb/lib/rpc_handler_interface.hpp>
#include <btcb/lib/rpcconfig.hpp>

namespace btcb
{
class rpc_handler_interface;

class rpc
{
public:
	rpc (boost::asio::io_context & io_ctx_a, btcb::rpc_config const & config_a, btcb::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc ();
	void start ();
	virtual void accept ();
	void stop ();

	btcb::rpc_config config;
	boost::asio::ip::tcp::acceptor acceptor;
	btcb::logger_mt logger;
	boost::asio::io_context & io_ctx;
	btcb::rpc_handler_interface & rpc_handler_interface;
	bool stopped{ false };
};

/** Returns the correct RPC implementation based on TLS configuration */
std::unique_ptr<btcb::rpc> get_rpc (boost::asio::io_context & io_ctx_a, btcb::rpc_config const & config_a, btcb::rpc_handler_interface & rpc_handler_interface_a);
}
