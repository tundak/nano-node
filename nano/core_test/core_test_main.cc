#include "gtest/gtest.h"
namespace btcb
{
void cleanup_test_directories_on_exit ();
void force_btcb_test_network ();
}
GTEST_API_ int main (int argc, char ** argv)
{
	printf ("Running main() from core_test_main.cc\n");
	btcb::force_btcb_test_network ();
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	btcb::cleanup_test_directories_on_exit ();
	return res;
}
