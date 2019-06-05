#pragma once

#include <boost/property_tree/ptree.hpp>
#include <functional>
#include <string>

namespace btcb
{
class rpc_config;
class rpc_handler_interface;
class logger_mt;

class rpc_handler : public std::enable_shared_from_this<btcb::rpc_handler>
{
public:
	rpc_handler (btcb::rpc_config const & rpc_config, std::string const & body_a, std::string const & request_id_a, std::function<void(std::string const &)> const & response_a, btcb::rpc_handler_interface & rpc_handler_interface_a, btcb::logger_mt & logger);
	void process_request ();
	void read (std::shared_ptr<std::vector<uint8_t>> req, std::shared_ptr<std::vector<uint8_t>> res, const std::string & action);

private:
	std::string body;
	std::string request_id;
	boost::property_tree::ptree request;
	std::function<void(std::string const &)> response;
	btcb::rpc_config const & rpc_config;
	btcb::rpc_handler_interface & rpc_handler_interface;
	btcb::logger_mt & logger;
};
}
