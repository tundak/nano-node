#include <btcb/crypto_lib/random_pool.hpp>
#include <btcb/lib/errors.hpp>
#include <btcb/lib/jsonconfig.hpp>
#include <btcb/lib/rpcconfig.hpp>
#include <btcb/lib/utility.hpp>
#include <btcb/btcb_wallet/icon.hpp>
#include <btcb/node/cli.hpp>
#include <btcb/node/ipc.hpp>
#include <btcb/node/json_handler.hpp>
#include <btcb/node/node_rpc_config.hpp>
#include <btcb/node/working.hpp>
#include <btcb/qt/qt.hpp>
#include <btcb/rpc/rpc.hpp>

#include <boost/make_shared.hpp>
#include <boost/program_options.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#ifndef BOOST_PROCESS_SUPPORTED
#error BOOST_PROCESS_SUPPORTED must be set, check configuration
#endif

#if BOOST_PROCESS_SUPPORTED
#include <boost/process.hpp>
#endif

class qt_wallet_config
{
public:
	qt_wallet_config (boost::filesystem::path const & data_path_a)
	{
		btcb::random_pool::generate_block (wallet.bytes.data (), wallet.bytes.size ());
		assert (!wallet.is_zero ());
	}
	bool upgrade_json (unsigned version_a, btcb::jsonconfig & json)
	{
		json.put ("version", json_version ());
		switch (version_a)
		{
			case 1:
			{
				btcb::account account;
				account.decode_account (json.get<std::string> ("account"));
				json.erase ("account");
				json.put ("account", account.to_account ());
				json.erase ("version");
			}
			case 2:
			{
				btcb::jsonconfig rpc_l;
				rpc.serialize_json (rpc_l);
				json.put ("rpc_enable", "false");
				json.put_child ("rpc", rpc_l);
				json.erase ("version");
			}
			case 3:
			{
				auto opencl_enable_l (json.get_optional<bool> ("opencl_enable"));
				if (!opencl_enable_l)
				{
					json.put ("opencl_enable", "false");
				}
				auto opencl_l (json.get_optional_child ("opencl"));
				if (!opencl_l)
				{
					btcb::jsonconfig opencl_l;
					opencl.serialize_json (opencl_l);
					json.put_child ("opencl", opencl_l);
				}
			}
			case 4:
				break;
			default:
				throw std::runtime_error ("Unknown qt_wallet_config version");
		}
		return version_a < json_version ();
	}

	btcb::error deserialize_json (bool & upgraded_a, btcb::jsonconfig & json)
	{
		if (!json.empty ())
		{
			auto version_l (json.get_optional<unsigned> ("version"));
			if (!version_l)
			{
				version_l = 1;
				json.put ("version", version_l.get ());
				upgraded_a = true;
			}

			upgraded_a |= upgrade_json (version_l.get (), json);
			auto wallet_l (json.get<std::string> ("wallet"));
			auto account_l (json.get<std::string> ("account"));
			auto node_l (json.get_required_child ("node"));
			auto rpc_l (json.get_required_child ("rpc"));
			rpc_enable = json.get<bool> ("rpc_enable");
			opencl_enable = json.get<bool> ("opencl_enable");
			auto opencl_l (json.get_required_child ("opencl"));

			if (wallet.decode_hex (wallet_l))
			{
				json.get_error ().set ("Invalid wallet id. Did you open a node daemon config?");
			}
			else if (account.decode_account (account_l))
			{
				json.get_error ().set ("Invalid account");
			}
			if (!node_l.get_error ())
			{
				node.deserialize_json (upgraded_a, node_l);
			}
			if (!rpc_l.get_error ())
			{
				rpc.deserialize_json (upgraded_a, rpc_l, data_path);
			}
			if (!opencl_l.get_error ())
			{
				opencl.deserialize_json (opencl_l);
			}
			if (wallet.is_zero ())
			{
				btcb::random_pool::generate_block (wallet.bytes.data (), wallet.bytes.size ());
				upgraded_a = true;
			}
		}
		else
		{
			serialize_json (json);
			upgraded_a = true;
		}
		return json.get_error ();
	}

	void serialize_json (btcb::jsonconfig & json)
	{
		std::string wallet_string;
		wallet.encode_hex (wallet_string);
		json.put ("version", json_version ());
		json.put ("wallet", wallet_string);
		json.put ("account", account.to_account ());
		btcb::jsonconfig node_l;
		node.enable_voting = false;
		node.bootstrap_connections_max = 4;
		node.serialize_json (node_l);
		json.put_child ("node", node_l);
		json.put ("rpc_enable", rpc_enable);
		btcb::jsonconfig rpc_l;
		rpc.serialize_json (rpc_l);
		json.put_child ("rpc", rpc_l);
		json.put ("opencl_enable", opencl_enable);
		btcb::jsonconfig opencl_l;
		opencl.serialize_json (opencl_l);
		json.put_child ("opencl", opencl_l);
	}

	bool serialize_json_stream (std::ostream & stream_a)
	{
		auto result (false);
		stream_a.seekp (0);
		try
		{
			btcb::jsonconfig json;
			serialize_json (json);
			json.write (stream_a);
		}
		catch (std::runtime_error const & ex)
		{
			std::cerr << ex.what () << std::endl;
			result = true;
		}
		return result;
	}

	btcb::uint256_union wallet;
	btcb::account account{ 0 };
	btcb::node_config node;
	bool rpc_enable{ false };
	btcb::node_rpc_config rpc;
	bool opencl_enable{ false };
	btcb::opencl_config opencl;
	boost::filesystem::path data_path;
	int json_version () const
	{
		return 4;
	}
};

namespace
{
void show_error (std::string const & message_a)
{
	QMessageBox message (QMessageBox::Critical, "Error starting Btcb", message_a.c_str ());
	message.setModal (true);
	message.show ();
	message.exec ();
}
bool update_config (qt_wallet_config & config_a, boost::filesystem::path const & config_path_a)
{
	auto account (config_a.account);
	auto wallet (config_a.wallet);
	auto error (false);
	btcb::jsonconfig config;
	if (!config.read_and_update (config_a, config_path_a))
	{
		if (account != config_a.account || wallet != config_a.wallet)
		{
			config_a.account = account;
			config_a.wallet = wallet;

			// Update json file with new account and/or wallet values
			std::fstream config_file;
			config_file.open (config_path_a.string (), std::ios_base::out | std::ios_base::trunc);
			boost::system::error_code error_chmod;
			btcb::set_secure_perm_file (config_path_a, error_chmod);
			error = config_a.serialize_json_stream (config_file);
		}
	}
	return error;
}
}

int run_wallet (QApplication & application, int argc, char * const * argv, boost::filesystem::path const & data_path)
{
	btcb_qt::eventloop_processor processor;
	boost::system::error_code error_chmod;
	boost::filesystem::create_directories (data_path);
	btcb::set_secure_perm_directory (data_path, error_chmod);
	QPixmap pixmap (":/logo.png");
	QSplashScreen * splash = new QSplashScreen (pixmap);
	splash->show ();
	application.processEvents ();
	splash->showMessage (QSplashScreen::tr ("Remember - Back Up Your Wallet Seed"), Qt::AlignBottom | Qt::AlignHCenter, Qt::darkGray);
	application.processEvents ();
	qt_wallet_config config (data_path);
	auto config_path (btcb::get_config_path (data_path));
	int result (0);
	btcb::jsonconfig json;
	auto error (json.read_and_update (config, config_path));
	btcb::set_secure_perm_file (config_path, error_chmod);
	if (!error)
	{
		config.node.logging.init (data_path);
		btcb::logger_mt logger{ config.node.logging.min_time_between_log_output };

		boost::asio::io_context io_ctx;
		btcb::thread_runner runner (io_ctx, config.node.io_threads);

		std::shared_ptr<btcb::node> node;
		std::shared_ptr<btcb_qt::wallet> gui;
		btcb::set_application_icon (application);
		auto opencl (btcb::opencl_work::create (config.opencl_enable, config.opencl, logger));
		btcb::work_pool work (config.node.work_threads, config.node.pow_sleep_interval, opencl ? [&opencl](btcb::uint256_union const & root_a, uint64_t difficulty_a) {
			return opencl->generate_work (root_a, difficulty_a);
		}
		                                                                                       : std::function<boost::optional<uint64_t> (btcb::uint256_union const &, uint64_t)> (nullptr));
		btcb::alarm alarm (io_ctx);
		btcb::node_init init;
		btcb::node_flags flags;

		node = std::make_shared<btcb::node> (init, io_ctx, data_path, alarm, config.node, work, flags);
		if (!init.error ())
		{
			auto wallet (node->wallets.open (config.wallet));
			if (wallet == nullptr)
			{
				auto existing (node->wallets.items.begin ());
				if (existing != node->wallets.items.end ())
				{
					wallet = existing->second;
					config.wallet = existing->first;
				}
				else
				{
					wallet = node->wallets.create (config.wallet);
				}
			}
			if (config.account.is_zero () || !wallet->exists (config.account))
			{
				auto transaction (wallet->wallets.tx_begin_write ());
				auto existing (wallet->store.begin (transaction));
				if (existing != wallet->store.end ())
				{
					btcb::uint256_union account (existing->first);
					config.account = account;
				}
				else
				{
					config.account = wallet->deterministic_insert (transaction);
				}
			}
			assert (wallet->exists (config.account));
			update_config (config, config_path);
			node->start ();
			btcb::ipc::ipc_server ipc (*node, config.rpc);

#if BOOST_PROCESS_SUPPORTED
			std::unique_ptr<boost::process::child> rpc_process;
#endif
			std::unique_ptr<btcb::rpc> rpc;
			std::unique_ptr<btcb::rpc_handler_interface> rpc_handler;
			if (config.rpc_enable)
			{
				if (!config.rpc.child_process.enable)
				{
					// Launch rpc in-process
					btcb::rpc_config rpc_config;
					auto error = btcb::read_and_update_rpc_config (data_path, rpc_config);
					if (error)
					{
						throw std::runtime_error ("Could not deserialize rpc_config file");
					}
					rpc_handler = std::make_unique<btcb::inprocess_rpc_handler> (*node, config.rpc);
					rpc = btcb::get_rpc (io_ctx, rpc_config, *rpc_handler);
					rpc->start ();
				}
				else
				{
					// Spawn a child rpc process
					if (!boost::filesystem::exists (config.rpc.child_process.rpc_path))
					{
						throw std::runtime_error (std::string ("RPC is configured to spawn a new process however the file cannot be found at: ") + config.rpc.child_process.rpc_path);
					}

#if BOOST_PROCESS_SUPPORTED
					auto network = node->network_params.network.get_current_network_as_string ();
					rpc_process = std::make_unique<boost::process::child> (config.rpc.child_process.rpc_path, "--daemon", "--data_path", data_path, "--network", network);
#else
					show_error ("rpc_enable is set to true in the config. Set it to false and start the RPC server manually.");
#endif
				}
			}
			QObject::connect (&application, &QApplication::aboutToQuit, [&]() {
				ipc.stop ();
				node->stop ();
				if (rpc)
				{
					rpc->stop ();
				}
#if USE_BOOST_PROCESS
				if (rpc_process)
				{
					rpc_process->terminate ();
				}
#endif
				runner.stop_event_processing ();
			});
			application.postEvent (&processor, new btcb_qt::eventloop_event ([&]() {
				gui = std::make_shared<btcb_qt::wallet> (application, processor, *node, wallet, config.account);
				splash->close ();
				gui->start ();
				gui->client_window->show ();
			}));
			result = application.exec ();
			runner.join ();
		}
		else
		{
			splash->hide ();
			show_error ("Error initializing node");
		}
		update_config (config, config_path);
	}
	else
	{
		splash->hide ();
		show_error ("Error deserializing config: " + json.get_error ().get_message ());
	}
	return result;
}

int main (int argc, char * const * argv)
{
	btcb::set_umask ();

	try
	{
		QApplication application (argc, const_cast<char **> (argv));
		boost::program_options::options_description description ("Command line options");
		description.add_options () ("help", "Print out options");
		btcb::add_node_options (description);
		boost::program_options::variables_map vm;
		boost::program_options::store (boost::program_options::command_line_parser (argc, argv).options (description).allow_unregistered ().run (), vm);
		boost::program_options::notify (vm);
		int result (0);

		auto network (vm.find ("network"));
		if (network != vm.end ())
		{
			auto err (btcb::network_constants::set_active_network (network->second.as<std::string> ()));
			if (err)
			{
				std::cerr << err.get_message () << std::endl;
				std::exit (1);
			}
		}

		if (!vm.count ("data_path"))
		{
			std::string error_string;
			if (!btcb::migrate_working_path (error_string))
			{
				throw std::runtime_error (error_string);
			}
		}

		auto ec = btcb::handle_node_options (vm);
		if (ec == btcb::error_cli::unknown_command)
		{
			if (vm.count ("help") != 0)
			{
				std::cout << description << std::endl;
			}
			else
			{
				try
				{
					boost::filesystem::path data_path;
					if (vm.count ("data_path"))
					{
						auto name (vm["data_path"].as<std::string> ());
						data_path = boost::filesystem::path (name);
					}
					else
					{
						data_path = btcb::working_path ();
					}
					result = run_wallet (application, argc, argv, data_path);
				}
				catch (std::exception const & e)
				{
					show_error (boost::str (boost::format ("Exception while running wallet: %1%") % e.what ()));
				}
				catch (...)
				{
					show_error ("Unknown exception while running wallet");
				}
			}
		}
		return result;
	}
	catch (std::exception const & e)
	{
		std::cerr << boost::str (boost::format ("Exception while initializing %1%") % e.what ());
	}
	catch (...)
	{
		std::cerr << boost::str (boost::format ("Unknown exception while initializing"));
	}
	return 1;
}
