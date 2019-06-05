#pragma once

#include <array>
#include <boost/filesystem.hpp>
#include <chrono>
#include <btcb/lib/errors.hpp>
#include <btcb/lib/numbers.hpp>
#include <string>

#define xstr(a) ver_str (a)
#define ver_str(a) #a

/**
* Returns build version information
*/
static const char * BTCB_MAJOR_MINOR_VERSION = xstr (BTCB_VERSION_MAJOR) "." xstr (BTCB_VERSION_MINOR);
static const char * BTCB_MAJOR_MINOR_RC_VERSION = xstr (BTCB_VERSION_MAJOR) "." xstr (BTCB_VERSION_MINOR) "RC" xstr (BTCB_VERSION_PATCH);

namespace btcb
{
/**
 * Network variants with different genesis blocks and network parameters
 * @warning Enum values are used in integral comparisons; do not change.
 */
enum class btcb_networks
{
	// Low work parameters, publicly known genesis key, test IP ports
	btcb_test_network = 0,
	rai_test_network = 0,
	// Normal work parameters, secret beta genesis key, beta IP ports
	btcb_beta_network = 1,
	rai_beta_network = 1,
	// Normal work parameters, secret live key, live IP ports
	btcb_live_network = 2,
	rai_live_network = 2,
};

class network_constants
{
public:
	network_constants () :
	network_constants (network_constants::active_network)
	{
	}

	network_constants (btcb_networks network_a) :
	current_network (network_a)
	{
		// Local work threshold for rate-limiting publishing blocks. ~5 seconds of work.
		uint64_t constexpr publish_test_threshold = 0xff00000000000000;
		uint64_t constexpr publish_full_threshold = 0xfffffe0000000000;
		publish_threshold = is_test_network () ? publish_test_threshold : publish_full_threshold;

		default_node_port = is_live_network () ? 9075 : is_beta_network () ? 34000 : 44000;
		default_rpc_port = is_live_network () ? 9076 : is_beta_network () ? 35000 : 45000;
		default_ipc_port = is_live_network () ? 9077 : is_beta_network () ? 36000 : 46000;
		default_websocket_port = is_live_network () ? 8078 : is_beta_network () ? 37000 : 47000;
		request_interval_ms = is_test_network () ? 20 : 16000;
		// Increase interval for test TSAN/ASAN builds
#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
		request_interval_ms = is_test_network () ? 100 : 16000;
#endif
#endif
	}

	/** The network this param object represents. This may differ from the global active network; this is needed for certain --debug... commands */
	btcb_networks current_network;
	uint64_t publish_threshold;
	uint16_t default_node_port;
	uint16_t default_rpc_port;
	uint16_t default_ipc_port;
	uint16_t default_websocket_port;
	unsigned request_interval_ms;

	/** Returns the network this object contains values for */
	btcb_networks network () const
	{
		return current_network;
	}

	/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network
	 */
	static void set_active_network (btcb_networks network_a)
	{
		active_network = network_a;
	}

	/**
	 * Optionally called on startup to override the global active network.
	 * If not called, the compile-time option will be used.
	 * @param network_a The new active network. Valid values are "live", "beta" and "test"
	 */
	static btcb::error set_active_network (std::string network_a)
	{
		btcb::error err;
		if (network_a == "live")
		{
			active_network = btcb::btcb_networks::btcb_live_network;
		}
		else if (network_a == "beta")
		{
			active_network = btcb::btcb_networks::btcb_beta_network;
		}
		else if (network_a == "test")
		{
			active_network = btcb::btcb_networks::btcb_test_network;
		}
		else
		{
			err = "Invalid network. Valid values are live, beta and test.";
		}
		return err;
	}

	const char * get_current_network_as_string () const
	{
		return is_live_network () ? "live" : is_beta_network () ? "beta" : "test";
	}

	bool is_live_network () const
	{
		return current_network == btcb_networks::btcb_live_network;
	}
	bool is_beta_network () const
	{
		return current_network == btcb_networks::btcb_beta_network;
	}
	bool is_test_network () const
	{
		return current_network == btcb_networks::btcb_test_network;
	}

	/** Initial value is ACTIVE_NETWORK compile flag, but can be overridden by a CLI flag */
	static btcb::btcb_networks active_network;
};

inline boost::filesystem::path get_config_path (boost::filesystem::path const & data_path)
{
	return data_path / "config.json";
}

inline boost::filesystem::path get_rpc_config_path (boost::filesystem::path const & data_path)
{
	return data_path / "rpc_config.json";
}

/** Called by gtest_main to enforce test network */
void force_btcb_test_network ();
}
