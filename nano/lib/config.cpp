#include <nano/lib/config.hpp>

namespace nano
{
void force_nano_test_network ()
{
	nano::network_constants::set_active_network (nano::nano_networks::btcb_test_network);
}
}
