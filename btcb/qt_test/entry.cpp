#include <QApplication>
#include <gtest/gtest.h>
QApplication * test_application = nullptr;
namespace btcb
{
void cleanup_test_directories_on_exit ();
void force_btcb_test_network ();
}

int main (int argc, char ** argv)
{
	btcb::force_btcb_test_network ();
	QApplication application (argc, argv);
	test_application = &application;
	testing::InitGoogleTest (&argc, argv);
	auto res = RUN_ALL_TESTS ();
	btcb::cleanup_test_directories_on_exit ();
	return res;
}
