#pragma once

#include <chrono>
#include <btcb/lib/errors.hpp>
#include <btcb/lib/utility.hpp>
#include <btcb/node/node.hpp>

namespace btcb
{
/** Test-system related error codes */
enum class error_system
{
	generic = 1,
	deadline_expired
};
class system final
{
public:
	system ();
	system (uint16_t, uint16_t, btcb::transport::transport_type = btcb::transport::transport_type::tcp);
	~system ();
	void generate_activity (btcb::node &, std::vector<btcb::account> &);
	void generate_mass_activity (uint32_t, btcb::node &);
	void generate_usage_traffic (uint32_t, uint32_t, size_t);
	void generate_usage_traffic (uint32_t, uint32_t);
	btcb::account get_random_account (std::vector<btcb::account> &);
	btcb::uint128_t get_random_amount (btcb::transaction const &, btcb::node &, btcb::account const &);
	void generate_rollback (btcb::node &, std::vector<btcb::account> &);
	void generate_change_known (btcb::node &, std::vector<btcb::account> &);
	void generate_change_unknown (btcb::node &, std::vector<btcb::account> &);
	void generate_receive (btcb::node &);
	void generate_send_new (btcb::node &, std::vector<btcb::account> &);
	void generate_send_existing (btcb::node &, std::vector<btcb::account> &);
	std::shared_ptr<btcb::wallet> wallet (size_t);
	btcb::account account (btcb::transaction const &, size_t);
	/**
	 * Polls, sleep if there's no work to be done (default 50ms), then check the deadline
	 * @returns 0 or btcb::deadline_expired
	 */
	std::error_code poll (const std::chrono::nanoseconds & sleep_time = std::chrono::milliseconds (50));
	void stop ();
	void deadline_set (const std::chrono::duration<double, std::nano> & delta);
	std::shared_ptr<btcb::node> add_node (btcb::node_config const &, bool = false, btcb::transport::transport_type = btcb::transport::transport_type::tcp);
	boost::asio::io_context io_ctx;
	btcb::alarm alarm{ io_ctx };
	std::vector<std::shared_ptr<btcb::node>> nodes;
	btcb::logging logging;
	btcb::work_pool work{ 1 };
	std::chrono::time_point<std::chrono::steady_clock, std::chrono::duration<double>> deadline{ std::chrono::steady_clock::time_point::max () };
	double deadline_scaling_factor{ 1.0 };
};
class landing_store final
{
public:
	landing_store () = default;
	landing_store (btcb::account const &, btcb::account const &, uint64_t, uint64_t);
	landing_store (bool &, std::istream &);
	btcb::account source;
	btcb::account destination;
	uint64_t start;
	uint64_t last;
	void serialize (std::ostream &) const;
	bool deserialize (std::istream &);
	bool operator== (btcb::landing_store const &) const;
};
class landing final
{
public:
	landing (btcb::node &, std::shared_ptr<btcb::wallet>, btcb::landing_store &, boost::filesystem::path const &);
	void write_store ();
	btcb::uint128_t distribution_amount (uint64_t);
	void distribute_one ();
	void distribute_ongoing ();
	boost::filesystem::path path;
	btcb::landing_store & store;
	std::shared_ptr<btcb::wallet> wallet;
	btcb::node & node;
	static int constexpr interval_exponent = 10;
	static std::chrono::seconds constexpr distribution_interval = std::chrono::seconds (1 << interval_exponent); // 1024 seconds
	static std::chrono::seconds constexpr sleep_seconds = std::chrono::seconds (7);
};
}
REGISTER_ERROR_CODES (btcb, error_system);
