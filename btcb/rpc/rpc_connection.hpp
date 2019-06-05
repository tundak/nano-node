#pragma once

#include <atomic>
#include <boost/beast.hpp>
#include <btcb/rpc/rpc_handler.hpp>

namespace btcb
{
class logger_mt;
class rpc_config;
class rpc_handler_interface;

class rpc_connection : public std::enable_shared_from_this<btcb::rpc_connection>
{
public:
	rpc_connection (btcb::rpc_config const & rpc_config, boost::asio::io_context & io_ctx, btcb::logger_mt & logger, btcb::rpc_handler_interface & rpc_handler_interface_a);
	virtual ~rpc_connection () = default;
	virtual void parse_connection ();
	virtual void write_completion_handler (std::shared_ptr<btcb::rpc_connection> rpc_connection);
	void prepare_head (unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);
	void write_result (std::string body, unsigned version, boost::beast::http::status status = boost::beast::http::status::ok);
	void parse_request (std::shared_ptr<boost::beast::http::request_parser<boost::beast::http::empty_body>> header_parser);

	void read ();

	boost::asio::ip::tcp::socket socket;
	boost::beast::flat_buffer buffer;
	boost::beast::http::response<boost::beast::http::string_body> res;
	std::atomic_flag responded;
	boost::asio::io_context & io_ctx;
	btcb::logger_mt & logger;
	btcb::rpc_config const & rpc_config;
	btcb::rpc_handler_interface & rpc_handler_interface;
};
}
