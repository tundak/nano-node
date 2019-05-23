#include <btcb/lib/config.hpp>

namespace btcb
{
void force_btcb_test_network ()
{
	btcb::network_constants::set_active_network (btcb::btcb_networks::btcb_test_network);
}
}
