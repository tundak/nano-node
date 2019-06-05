#pragma once

#include <boost/filesystem.hpp>
#include <boost/log/sources/logger.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <cstdint>
#include <btcb/lib/errors.hpp>
#include <btcb/lib/jsonconfig.hpp>
#include <btcb/lib/logger_mt.hpp>

#define FATAL_LOG_PREFIX "FATAL ERROR: "

namespace btcb
{
class logging final
{
public:
	btcb::error serialize_json (btcb::jsonconfig &) const;
	btcb::error deserialize_json (bool &, btcb::jsonconfig &);
	bool upgrade_json (unsigned, btcb::jsonconfig &);
	bool ledger_logging () const;
	bool ledger_duplicate_logging () const;
	bool vote_logging () const;
	bool network_logging () const;
	bool network_timeout_logging () const;
	bool network_message_logging () const;
	bool network_publish_logging () const;
	bool network_packet_logging () const;
	bool network_keepalive_logging () const;
	bool network_node_id_handshake_logging () const;
	bool node_lifetime_tracing () const;
	bool insufficient_work_logging () const;
	bool upnp_details_logging () const;
	bool timing_logging () const;
	bool log_ipc () const;
	bool bulk_pull_logging () const;
	bool callback_logging () const;
	bool work_generation_time () const;
	bool log_to_cerr () const;
	void init (boost::filesystem::path const &);

	bool ledger_logging_value{ false };
	bool ledger_duplicate_logging_value{ false };
	bool vote_logging_value{ false };
	bool network_logging_value{ true };
	bool network_timeout_logging_value{ false };
	bool network_message_logging_value{ false };
	bool network_publish_logging_value{ false };
	bool network_packet_logging_value{ false };
	bool network_keepalive_logging_value{ false };
	bool network_node_id_handshake_logging_value{ false };
	bool node_lifetime_tracing_value{ false };
	bool insufficient_work_logging_value{ true };
	bool log_ipc_value{ true };
	bool bulk_pull_logging_value{ false };
	bool work_generation_time_value{ true };
	bool upnp_details_logging_value{ false };
	bool timing_logging_value{ false };
	bool log_to_cerr_value{ false };
	bool flush{ true };
	uintmax_t max_size{ 128 * 1024 * 1024 };
	uintmax_t rotation_size{ 4 * 1024 * 1024 };
	std::chrono::milliseconds min_time_between_log_output{ 5 };
	static void release_file_sink ();
	int json_version () const
	{
		return 7;
	}

private:
	static boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend>> file_sink;
	static std::atomic_flag logging_already_added;
};
}
