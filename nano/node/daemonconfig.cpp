#include <btcb/lib/config.hpp>
#include <btcb/node/daemonconfig.hpp>

btcb::daemon_config::daemon_config (boost::filesystem::path const & data_path_a) :
data_path (data_path_a)
{
}

btcb::error btcb::daemon_config::serialize_json (btcb::jsonconfig & json)
{
	json.put ("version", json_version ());
	json.put ("rpc_enable", rpc_enable);

	btcb::jsonconfig rpc_l;
	rpc.serialize_json (rpc_l);
	json.put_child ("rpc", rpc_l);

	btcb::jsonconfig node_l;
	node.serialize_json (node_l);
	btcb::jsonconfig node (node_l);
	json.put_child ("node", node);

	json.put ("opencl_enable", opencl_enable);
	btcb::jsonconfig opencl_l;
	opencl.serialize_json (opencl_l);
	json.put_child ("opencl", opencl_l);
	return json.get_error ();
}

btcb::error btcb::daemon_config::deserialize_json (bool & upgraded_a, btcb::jsonconfig & json)
{
	try
	{
		if (!json.empty ())
		{
			int version_l;
			json.get_optional<int> ("version", version_l);

			upgraded_a |= upgrade_json (version_l, json);

			json.get_optional<bool> ("rpc_enable", rpc_enable);

			auto rpc_l (json.get_required_child ("rpc"));

			if (!rpc.deserialize_json (upgraded_a, rpc_l, data_path))
			{
				auto node_l (json.get_required_child ("node"));
				if (!json.get_error ())
				{
					node.deserialize_json (upgraded_a, node_l);
				}
			}

			if (!json.get_error ())
			{
				json.get_required<bool> ("opencl_enable", opencl_enable);
				auto opencl_l (json.get_required_child ("opencl"));
				if (!json.get_error ())
				{
					opencl.deserialize_json (opencl_l);
				}
			}
		}
		else
		{
			upgraded_a = true;
			serialize_json (json);
		}
	}
	catch (std::runtime_error const & ex)
	{
		json.get_error () = ex;
	}
	return json.get_error ();
}

bool btcb::daemon_config::upgrade_json (unsigned version_a, btcb::jsonconfig & json)
{
	json.put ("version", json_version ());
	switch (version_a)
	{
		case 1:
		{
			bool opencl_enable_l;
			json.get_optional<bool> ("opencl_enable", opencl_enable_l);
			if (!opencl_enable_l)
			{
				json.put ("opencl_enable", false);
			}
			auto opencl_l (json.get_optional_child ("opencl"));
			if (!opencl_l)
			{
				btcb::jsonconfig opencl_l;
				opencl.serialize_json (opencl_l);
				json.put_child ("opencl", opencl_l);
			}
		}
		case 2:
			break;
		default:
			throw std::runtime_error ("Unknown daemon_config version");
	}
	return version_a < json_version ();
}

namespace btcb
{
btcb::error read_and_update_daemon_config (boost::filesystem::path const & data_path, btcb::daemon_config & config_a)
{
	boost::system::error_code error_chmod;
	btcb::jsonconfig json;
	auto config_path = btcb::get_config_path (data_path);
	auto error (json.read_and_update (config_a, config_path));
	btcb::set_secure_perm_file (config_path, error_chmod);
	return error;
}
}
