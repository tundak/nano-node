#pragma once

#include <btcb/lib/errors.hpp>
#include <btcb/node/node_rpc_config.hpp>
#include <btcb/node/nodeconfig.hpp>
#include <btcb/node/openclconfig.hpp>

namespace btcb
{
class daemon_config
{
public:
	daemon_config (boost::filesystem::path const & data_path);
	btcb::error deserialize_json (bool &, btcb::jsonconfig &);
	btcb::error serialize_json (btcb::jsonconfig &);
	/**
	 * Returns true if an upgrade occurred
	 * @param version The version to upgrade to.
	 * @param config Configuration to upgrade.
	 */
	bool upgrade_json (unsigned version, btcb::jsonconfig & config);
	bool rpc_enable{ false };
	btcb::node_rpc_config rpc;
	btcb::node_config node;
	bool opencl_enable{ false };
	btcb::opencl_config opencl;
	boost::filesystem::path data_path;
	int json_version () const
	{
		return 2;
	}
};

btcb::error read_and_update_daemon_config (boost::filesystem::path const &, btcb::daemon_config & config_a);
}
