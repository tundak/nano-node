#include <btcb/crypto_lib/random_pool.hpp>
#include <btcb/lib/config.hpp>
#include <btcb/node/testing.hpp>
#include <btcb/qt/qt.hpp>

#include <thread>

int main (int argc, char ** argv)
{
	btcb::network_constants::set_active_network (btcb::btcb_networks::btcb_test_network);
	QApplication application (argc, argv);
	QCoreApplication::setOrganizationName ("Btcb");
	QCoreApplication::setOrganizationDomain ("nano.org");
	QCoreApplication::setApplicationName ("Btcb Wallet");
	btcb_qt::eventloop_processor processor;
	static int count (16);
	btcb::system system (24000, count);
	btcb::thread_runner runner (system.io_ctx, system.nodes[0]->config.io_threads);
	std::unique_ptr<QTabWidget> client_tabs (new QTabWidget);
	std::vector<std::unique_ptr<btcb_qt::wallet>> guis;
	for (auto i (0); i < count; ++i)
	{
		btcb::uint256_union wallet_id;
		btcb::random_pool::generate_block (wallet_id.bytes.data (), wallet_id.bytes.size ());
		auto wallet (system.nodes[i]->wallets.create (wallet_id));
		btcb::keypair key;
		wallet->insert_adhoc (key.prv);
		guis.push_back (std::unique_ptr<btcb_qt::wallet> (new btcb_qt::wallet (application, processor, *system.nodes[i], wallet, key.pub)));
		client_tabs->addTab (guis.back ()->client_window, boost::str (boost::format ("Wallet %1%") % i).c_str ());
	}
	client_tabs->show ();
	QObject::connect (&application, &QApplication::aboutToQuit, [&]() {
		system.stop ();
	});
	int result;
	try
	{
		result = application.exec ();
	}
	catch (...)
	{
		result = -1;
		assert (false);
	}
	runner.join ();
	return result;
}
