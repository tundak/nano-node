#include <btcb/lib/jsonconfig.hpp>
#include <btcb/node/websocketconfig.hpp>

btcb::websocket::config::config () :
port (network_constants.default_websocket_port)
{
}

btcb::error btcb::websocket::config::serialize_json (btcb::jsonconfig & json) const
{
	json.put ("enable", enabled);
	json.put ("address", address.to_string ());
	json.put ("port", port);
	return json.get_error ();
}

btcb::error btcb::websocket::config::deserialize_json (btcb::jsonconfig & json)
{
	json.get<bool> ("enable", enabled);
	json.get_required<boost::asio::ip::address_v6> ("address", address);
	json.get<uint16_t> ("port", port);
	return json.get_error ();
}
