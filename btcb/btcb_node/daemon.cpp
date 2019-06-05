#include <boost/property_tree/json_parser.hpp>
#include <fstream>
#include <iostream>
#include <btcb/lib/utility.hpp>
#include <btcb/btcb_node/daemon.hpp>
#include <btcb/node/daemonconfig.hpp>
#include <btcb/node/ipc.hpp>
#include <btcb/node/json_handler.hpp>
#include <btcb/node/node.hpp>
#include <btcb/node/openclwork.hpp>
#include <btcb/node/working.hpp>
#include <btcb/rpc/rpc.hpp>

#ifndef BOOST_PROCESS_SUPPORTED
#error BOOST_PROCESS_SUPPORTED must be set, check configuration
#endif

#if BOOST_PROCESS_SUPPORTED
#include <boost/process.hpp>
#endif

void btcb_daemon::daemon::run (boost::filesystem::path const & data_path, btcb::node_flags const & flags)
{
	boost::filesystem::create_directories (data_path);
	boost::system::error_code error_chmod;
	btcb::set_secure_perm_directory (data_path, error_chmod);
	std::unique_ptr<btcb::thread_runner> runner;
	btcb::daemon_config config (data_path);
	auto error = btcb::read_and_update_daemon_config (data_path, config);

	if (!error)
	{
		config.node.logging.init (data_path);
		btcb::logger_mt logger{ config.node.logging.min_time_between_log_output };
		boost::asio::io_context io_ctx;
		auto opencl (btcb::opencl_work::create (config.opencl_enable, config.opencl, logger));
		btcb::work_pool opencl_work (config.node.work_threads, config.node.pow_sleep_interval, opencl ? [&opencl](btcb::uint256_union const & root_a, uint64_t difficulty_a) {
			return opencl->generate_work (root_a, difficulty_a);
		}
		                                                                                              : std::function<boost::optional<uint64_t> (btcb::uint256_union const &, uint64_t)> (nullptr));
		btcb::alarm alarm (io_ctx);
		btcb::node_init init;
		try
		{
			auto node (std::make_shared<btcb::node> (init, io_ctx, data_path, alarm, config.node, opencl_work, flags));
			if (!init.error ())
			{
				node->start ();
				btcb::ipc::ipc_server ipc_server (*node, config.rpc);
#if BOOST_PROCESS_SUPPORTED
				std::unique_ptr<boost::process::child> rpc_process;
#endif
				std::unique_ptr<std::thread> rpc_process_thread;
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
						rpc_handler = std::make_unique<btcb::inprocess_rpc_handler> (*node, config.rpc, [&ipc_server]() {
							ipc_server.stop ();
						});
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

						auto network = node->network_params.network.get_current_network_as_string ();
#if BOOST_PROCESS_SUPPORTED
						rpc_process = std::make_unique<boost::process::child> (config.rpc.child_process.rpc_path, "--daemon", "--data_path", data_path, "--network", network);
#else
						auto rpc_exe_command = boost::str (boost::format ("%1% --daemon --data_path=%2% --network=%3%") % config.rpc.child_process.rpc_path % data_path % network);
						// clang-format off
						rpc_process_thread = std::make_unique<std::thread> ([rpc_exe_command, &logger = node->logger]() {
							btcb::thread_role::set (btcb::thread_role::name::rpc_process_container);
							std::system (rpc_exe_command.c_str ());
							logger.always_log ("RPC server has stopped");
						});
						// clang-format on
#endif
					}
				}

				runner = std::make_unique<btcb::thread_runner> (io_ctx, node->config.io_threads);
				runner->join ();
#if BOOST_PROCESS_SUPPORTED
				if (rpc_process)
				{
					rpc_process->wait ();
				}
#else
				if (rpc_process_thread)
				{
					rpc_process_thread->join ();
				}
#endif
			}
			else
			{
				std::cerr << "Error initializing node\n";
			}
		}
		catch (const std::runtime_error & e)
		{
			std::cerr << "Error while running node (" << e.what () << ")\n";
		}
	}
	else
	{
		std::cerr << "Error deserializing config: " << error.get_message () << std::endl;
	}
}
