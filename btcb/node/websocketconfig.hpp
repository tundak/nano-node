#pragma once

#include <boost/asio.hpp>
#include <btcb/lib/config.hpp>
#include <btcb/lib/errors.hpp>

namespace btcb
{
class jsonconfig;
namespace websocket
{
	/** websocket configuration */
	class config final
	{
	public:
		config ();
		btcb::error deserialize_json (btcb::jsonconfig & json_a);
		btcb::error serialize_json (btcb::jsonconfig & json) const;
		btcb::network_constants network_constants;
		bool enabled{ false };
		uint16_t port;
		boost::asio::ip::address_v6 address{ boost::asio::ip::address_v6::loopback () };
	};
}
}
