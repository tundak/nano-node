#pragma once

#include <boost/asio/buffer.hpp>

#include <btcb/node/common.hpp>
#include <btcb/node/stats.hpp>

namespace btcb
{
namespace transport
{
	class message;
	btcb::endpoint map_endpoint_to_v6 (btcb::endpoint const &);
	btcb::endpoint map_tcp_to_endpoint (btcb::tcp_endpoint const &);
	btcb::tcp_endpoint map_endpoint_to_tcp (btcb::endpoint const &);
	// Unassigned, reserved, self
	bool reserved_address (btcb::endpoint const &, bool = false);
	// Maximum number of peers per IP
	static size_t constexpr max_peers_per_ip = 10;
	static std::chrono::seconds constexpr syn_cookie_cutoff = std::chrono::seconds (5);
	enum class transport_type : uint8_t
	{
		undefined = 0,
		udp = 1,
		tcp = 2
	};
	class channel
	{
	public:
		channel (btcb::node &);
		virtual ~channel () = default;
		virtual size_t hash_code () const = 0;
		virtual bool operator== (btcb::transport::channel const &) const = 0;
		void send (btcb::message const &, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr);
		virtual void send_buffer (std::shared_ptr<std::vector<uint8_t>>, btcb::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) = 0;
		virtual std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, btcb::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const = 0;
		virtual std::string to_string () const = 0;
		virtual btcb::endpoint get_endpoint () const = 0;
		virtual btcb::tcp_endpoint get_tcp_endpoint () const = 0;
		virtual btcb::transport::transport_type get_type () const = 0;

		std::chrono::steady_clock::time_point get_last_bootstrap_attempt () const
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return last_bootstrap_attempt;
		}

		void set_last_bootstrap_attempt (std::chrono::steady_clock::time_point const time_a)
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			last_bootstrap_attempt = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_received () const
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return last_packet_received;
		}

		void set_last_packet_received (std::chrono::steady_clock::time_point const time_a)
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			last_packet_received = time_a;
		}

		std::chrono::steady_clock::time_point get_last_packet_sent () const
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return last_packet_sent;
		}

		void set_last_packet_sent (std::chrono::steady_clock::time_point const time_a)
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			last_packet_sent = time_a;
		}

		boost::optional<btcb::account> get_node_id () const
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return node_id;
		}

		void set_node_id (btcb::account node_id_a)
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			node_id = node_id_a;
		}

		unsigned get_network_version () const
		{
			return network_version;
		}

		void set_network_version (unsigned network_version_a)
		{
			network_version = network_version_a;
		}

		mutable std::mutex channel_mutex;

	private:
		std::chrono::steady_clock::time_point last_bootstrap_attempt{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_received{ std::chrono::steady_clock::time_point () };
		std::chrono::steady_clock::time_point last_packet_sent{ std::chrono::steady_clock::time_point () };
		boost::optional<btcb::account> node_id{ boost::none };
		std::atomic<unsigned> network_version{ btcb::protocol_version };

	protected:
		btcb::node & node;
	};
} // namespace transport
} // namespace btcb

namespace std
{
template <>
struct hash<::btcb::transport::channel>
{
	size_t operator() (::btcb::transport::channel const & channel_a) const
	{
		return channel_a.hash_code ();
	}
};
template <>
struct equal_to<std::reference_wrapper<::btcb::transport::channel const>>
{
	bool operator() (std::reference_wrapper<::btcb::transport::channel const> const & lhs, std::reference_wrapper<::btcb::transport::channel const> const & rhs) const
	{
		return lhs.get () == rhs.get ();
	}
};
}

namespace boost
{
template <>
struct hash<::btcb::transport::channel>
{
	size_t operator() (::btcb::transport::channel const & channel_a) const
	{
		std::hash<::btcb::transport::channel> hash;
		return hash (channel_a);
	}
};
template <>
struct hash<std::reference_wrapper<::btcb::transport::channel const>>
{
	size_t operator() (std::reference_wrapper<::btcb::transport::channel const> const & channel_a) const
	{
		std::hash<::btcb::transport::channel> hash;
		return hash (channel_a.get ());
	}
};
}
