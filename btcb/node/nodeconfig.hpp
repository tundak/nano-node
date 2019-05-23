#pragma once

#include <chrono>
#include <btcb/lib/config.hpp>
#include <btcb/lib/errors.hpp>
#include <btcb/lib/jsonconfig.hpp>
#include <btcb/lib/numbers.hpp>
#include <btcb/node/diagnosticsconfig.hpp>
#include <btcb/node/ipcconfig.hpp>
#include <btcb/node/logging.hpp>
#include <btcb/node/stats.hpp>
#include <btcb/node/websocketconfig.hpp>
#include <btcb/secure/common.hpp>
#include <vector>

namespace btcb
{
/**
 * Node configuration
 */
class node_config
{
public:
	node_config ();
	node_config (uint16_t, btcb::logging const &);
	btcb::error serialize_json (btcb::jsonconfig &) const;
	btcb::error deserialize_json (bool &, btcb::jsonconfig &);
	bool upgrade_json (unsigned, btcb::jsonconfig &);
	btcb::account random_representative ();
	btcb::network_params network_params;
	uint16_t peering_port{ 0 };
	btcb::logging logging;
	std::vector<std::pair<std::string, uint16_t>> work_peers;
	std::vector<std::string> preconfigured_peers;
	std::vector<btcb::account> preconfigured_representatives;
	unsigned bootstrap_fraction_numerator{ 1 };
	btcb::amount receive_minimum{ btcb::xrb_ratio };
	btcb::amount vote_minimum{ btcb::Gxrb_ratio };
	btcb::amount online_weight_minimum{ 60000 * btcb::Gxrb_ratio };
	unsigned online_weight_quorum{ 50 };
	unsigned password_fanout{ 1024 };
	unsigned io_threads{ std::max<unsigned> (4, boost::thread::hardware_concurrency ()) };
	unsigned network_threads{ std::max<unsigned> (4, boost::thread::hardware_concurrency ()) };
	unsigned work_threads{ std::max<unsigned> (4, boost::thread::hardware_concurrency ()) };
	unsigned signature_checker_threads{ (boost::thread::hardware_concurrency () != 0) ? boost::thread::hardware_concurrency () - 1 : 0 }; /* The calling thread does checks as well so remove it from the number of threads used */
	bool enable_voting{ false };
	unsigned bootstrap_connections{ 4 };
	unsigned bootstrap_connections_max{ 64 };
	btcb::websocket::config websocket_config;
	btcb::diagnostics_config diagnostics_config;
	std::string callback_address;
	uint16_t callback_port{ 0 };
	std::string callback_target;
	int lmdb_max_dbs{ 128 };
	bool allow_local_peers{ !network_params.network.is_live_network () }; // disable by default for live network
	btcb::stat_config stat_config;
	btcb::ipc::ipc_config ipc_config;
	btcb::uint256_union epoch_block_link;
	btcb::account epoch_block_signer;
	boost::asio::ip::address_v6 external_address{ boost::asio::ip::address_v6{} };
	uint16_t external_port{ 0 };
	std::chrono::milliseconds block_processor_batch_max_time{ std::chrono::milliseconds (5000) };
	std::chrono::seconds unchecked_cutoff_time{ std::chrono::seconds (4 * 60 * 60) }; // 4 hours
	/** Timeout for initiated async operations */
	std::chrono::seconds tcp_io_timeout{ network_params.network.is_test_network () ? std::chrono::seconds (5) : std::chrono::seconds (15) };
	/** Default maximum idle time for a socket before it's automatically closed */
	std::chrono::seconds tcp_idle_timeout{ std::chrono::minutes (2) };
	std::chrono::nanoseconds pow_sleep_interval{ 0 };
	/** Default maximum incoming TCP connections, including realtime network & bootstrap */
	unsigned tcp_incoming_connections_max{ 1024 };
	static std::chrono::seconds constexpr keepalive_period = std::chrono::seconds (60);
	static std::chrono::seconds constexpr keepalive_cutoff = keepalive_period * 5;
	static std::chrono::minutes constexpr wallet_backup_interval = std::chrono::minutes (5);
	static int json_version ()
	{
		return 17;
	}
};

class node_flags final
{
public:
	bool disable_backup{ false };
	bool disable_lazy_bootstrap{ false };
	bool disable_legacy_bootstrap{ false };
	bool disable_wallet_bootstrap{ false };
	bool disable_bootstrap_listener{ false };
	bool disable_unchecked_cleanup{ false };
	bool disable_unchecked_drop{ true };
	bool fast_bootstrap{ false };
	size_t sideband_batch_size{ 512 };
	size_t block_processor_batch_size{ 0 };
	size_t block_processor_full_size{ 65536 };
	size_t block_processor_verification_size{ 0 };
};
}
