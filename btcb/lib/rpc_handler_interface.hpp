#pragma once

namespace btcb
{
class rpc;

class rpc_handler_interface
{
public:
	virtual ~rpc_handler_interface () = default;
	virtual void process_request (std::string const & action, std::string const & body, std::function<void(std::string const &)> response) = 0;
	virtual void stop () = 0;
	virtual void rpc_instance (btcb::rpc & rpc) = 0;
};
}
