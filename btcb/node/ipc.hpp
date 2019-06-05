#pragma once

#include <atomic>
#include <btcb/lib/ipc.hpp>
#include <btcb/lib/numbers.hpp>
#include <btcb/node/node_rpc_config.hpp>

namespace btcb
{
class node;

namespace ipc
{
	/** The IPC server accepts connections on one or more configured transports */
	class ipc_server
	{
	public:
		ipc_server (btcb::node & node_a, btcb::node_rpc_config const & node_rpc_config);

		virtual ~ipc_server ();
		void stop ();

		btcb::node & node;
		btcb::node_rpc_config const & node_rpc_config;

		/** Unique counter/id shared across sessions */
		std::atomic<uint64_t> id_dispenser{ 0 };

	private:
		std::unique_ptr<dsock_file_remover> file_remover;
		std::vector<std::shared_ptr<btcb::ipc::transport>> transports;
	};
}
}
