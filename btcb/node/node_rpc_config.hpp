#pragma once

#include <boost/filesystem.hpp>
#include <btcb/lib/rpcconfig.hpp>
#include <string>

namespace btcb
{
class rpc_child_process_config final
{
public:
	bool enable{ false };
	std::string rpc_path{ get_default_rpc_filepath () };
};

class node_rpc_config final
{
public:
	btcb::error serialize_json (btcb::jsonconfig &) const;
	btcb::error deserialize_json (bool & upgraded_a, btcb::jsonconfig &, boost::filesystem::path const & data_path);
	bool enable_sign_hash{ false };
	uint64_t max_work_generate_difficulty{ 0xff00000000000000 };
	btcb::rpc_child_process_config child_process;
	static int json_version ()
	{
		return 1;
	}

private:
	void migrate (btcb::jsonconfig & json, boost::filesystem::path const & data_path);
};
}
