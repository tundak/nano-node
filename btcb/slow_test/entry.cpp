#include <gtest/gtest.h>
namespace btcb
{
void cleanup_test_directories_on_exit ();
void force_btcb_test_network ();
}

int main (int argc, char ** argv)
{
	btcb::force_btcb_test_network ();
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	btcb::cleanup_test_directories_on_exit ();
	return res;
}
