#pragma once

#include <mutex>
#include <btcb/node/common.hpp>
#include <btcb/node/stats.hpp>
#include <btcb/node/transport/transport.hpp>

namespace btcb
{
class message_buffer;
namespace transport
{
	class udp_channels;
	class channel_udp final : public btcb::transport::channel
	{
		friend class btcb::transport::udp_channels;

	public:
		channel_udp (btcb::transport::udp_channels &, btcb::endpoint const &, unsigned = btcb::protocol_version);
		size_t hash_code () const override;
		bool operator== (btcb::transport::channel const &) const override;
		void send_buffer (std::shared_ptr<std::vector<uint8_t>>, btcb::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) override;
		std::function<void(boost::system::error_code const &, size_t)> callback (std::shared_ptr<std::vector<uint8_t>>, btcb::stat::detail, std::function<void(boost::system::error_code const &, size_t)> const & = nullptr) const override;
		std::string to_string () const override;
		bool operator== (btcb::transport::channel_udp const & other_a) const
		{
			return &channels == &other_a.channels && endpoint == other_a.endpoint;
		}

		btcb::endpoint get_endpoint () const override
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return endpoint;
		}

		btcb::tcp_endpoint get_tcp_endpoint () const override
		{
			std::lock_guard<std::mutex> lk (channel_mutex);
			return btcb::transport::map_endpoint_to_tcp (endpoint);
		}

		btcb::transport::transport_type get_type () const override
		{
			return btcb::transport::transport_type::udp;
		}

	private:
		btcb::endpoint endpoint;
		btcb::transport::udp_channels & channels;
	};
	class udp_channels final
	{
		friend class btcb::transport::channel_udp;

	public:
		udp_channels (btcb::node &, uint16_t);
		std::shared_ptr<btcb::transport::channel_udp> insert (btcb::endpoint const &, unsigned);
		void erase (btcb::endpoint const &);
		size_t size () const;
		std::shared_ptr<btcb::transport::channel_udp> channel (btcb::endpoint const &) const;
		void random_fill (std::array<btcb::endpoint, 8> &) const;
		std::unordered_set<std::shared_ptr<btcb::transport::channel>> random_set (size_t) const;
		bool store_all (bool = true);
		std::shared_ptr<btcb::transport::channel_udp> find_node_id (btcb::account const &);
		void clean_node_id (btcb::endpoint const &, btcb::account const &);
		// Get the next peer for attempting a tcp bootstrap connection
		btcb::tcp_endpoint bootstrap_peer ();
		void receive ();
		void start ();
		void stop ();
		void send (boost::asio::const_buffer buffer_a, btcb::endpoint endpoint_a, std::function<void(boost::system::error_code const &, size_t)> const & callback_a);
		btcb::endpoint get_local_endpoint () const;
		void receive_action (btcb::message_buffer *);
		void process_packets ();
		std::shared_ptr<btcb::transport::channel> create (btcb::endpoint const &);
		bool max_ip_connections (btcb::endpoint const &);
		// Should we reach out to this endpoint with a keepalive message
		bool reachout (btcb::endpoint const &);
		std::unique_ptr<seq_con_info_component> collect_seq_con_info (std::string const &);
		void purge (std::chrono::steady_clock::time_point const &);
		void purge_syn_cookies (std::chrono::steady_clock::time_point const &);
		// Returns boost::none if the IP is rate capped on syn cookie requests,
		// or if the endpoint already has a syn cookie query
		boost::optional<btcb::uint256_union> assign_syn_cookie (btcb::endpoint const &);
		// Returns false if valid, true if invalid (true on error convention)
		// Also removes the syn cookie from the store if valid
		bool validate_syn_cookie (btcb::endpoint const &, btcb::account const &, btcb::signature const &);
		void ongoing_keepalive ();
		void list (std::deque<std::shared_ptr<btcb::transport::channel>> &);
		void modify (std::shared_ptr<btcb::transport::channel_udp>, std::function<void(std::shared_ptr<btcb::transport::channel_udp>)>);
		btcb::node & node;

	private:
		void close_socket ();
		void ongoing_syn_cookie_cleanup ();
		class endpoint_tag
		{
		};
		class ip_address_tag
		{
		};
		class random_access_tag
		{
		};
		class last_packet_received_tag
		{
		};
		class last_bootstrap_attempt_tag
		{
		};
		class node_id_tag
		{
		};
		class channel_udp_wrapper final
		{
		public:
			std::shared_ptr<btcb::transport::channel_udp> channel;
			btcb::endpoint endpoint () const
			{
				return channel->get_endpoint ();
			}
			std::chrono::steady_clock::time_point last_packet_received () const
			{
				return channel->get_last_packet_received ();
			}
			std::chrono::steady_clock::time_point last_bootstrap_attempt () const
			{
				return channel->get_last_bootstrap_attempt ();
			}
			boost::asio::ip::address ip_address () const
			{
				return endpoint ().address ();
			}
			btcb::account node_id () const
			{
				auto node_id_l (channel->get_node_id ());
				if (node_id_l.is_initialized ())
				{
					return node_id_l.get ();
				}
				else
				{
					return 0;
				}
			}
		};
		class endpoint_attempt final
		{
		public:
			btcb::endpoint endpoint;
			std::chrono::steady_clock::time_point last_attempt;
		};
		class syn_cookie_info final
		{
		public:
			btcb::uint256_union cookie;
			std::chrono::steady_clock::time_point created_at;
		};
		mutable std::mutex mutex;
		boost::multi_index_container<
		channel_udp_wrapper,
		boost::multi_index::indexed_by<
		boost::multi_index::random_access<boost::multi_index::tag<random_access_tag>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_bootstrap_attempt_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_bootstrap_attempt>>,
		boost::multi_index::hashed_unique<boost::multi_index::tag<endpoint_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, btcb::endpoint, &channel_udp_wrapper::endpoint>>,
		boost::multi_index::hashed_non_unique<boost::multi_index::tag<node_id_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, btcb::account, &channel_udp_wrapper::node_id>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<last_packet_received_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, std::chrono::steady_clock::time_point, &channel_udp_wrapper::last_packet_received>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::tag<ip_address_tag>, boost::multi_index::const_mem_fun<channel_udp_wrapper, boost::asio::ip::address, &channel_udp_wrapper::ip_address>>>>
		channels;
		boost::multi_index_container<
		endpoint_attempt,
		boost::multi_index::indexed_by<
		boost::multi_index::hashed_unique<boost::multi_index::member<endpoint_attempt, btcb::endpoint, &endpoint_attempt::endpoint>>,
		boost::multi_index::ordered_non_unique<boost::multi_index::member<endpoint_attempt, std::chrono::steady_clock::time_point, &endpoint_attempt::last_attempt>>>>
		attempts;
		std::unordered_map<btcb::endpoint, syn_cookie_info> syn_cookies;
		std::unordered_map<boost::asio::ip::address, unsigned> syn_cookies_per_ip;
		boost::asio::strand<boost::asio::io_context::executor_type> strand;
		boost::asio::ip::udp::socket socket;
		btcb::endpoint local_endpoint;
		std::atomic<bool> stopped{ false };
	};
} // namespace transport
} // namespace btcb
