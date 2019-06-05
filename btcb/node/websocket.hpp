#pragma once

#include <algorithm>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <btcb/lib/blocks.hpp>
#include <btcb/lib/numbers.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/* Boost v1.70 introduced breaking changes; the conditional compilation allows 1.6x to be supported as well. */
#if BOOST_VERSION < 107000
using socket_type = boost::asio::ip::tcp::socket;
#define beast_buffers boost::beast::buffers
#else
using socket_type = boost::asio::basic_stream_socket<boost::asio::ip::tcp, boost::asio::io_context::executor_type>;
#define beast_buffers boost::beast::make_printable
#endif

namespace btcb
{
class node;
namespace websocket
{
	class listener;

	/** Supported topics */
	enum class topic
	{
		invalid = 0,
		/** Acknowledgement of prior incoming message */
		ack,
		/** A confirmation message */
		confirmation,
		/** A vote message **/
		vote,
		/** Auxiliary length, not a valid topic, must be the last enum */
		_length
	};
	constexpr size_t number_topics{ static_cast<size_t> (topic::_length) - static_cast<size_t> (topic::invalid) };

	/** A message queued for broadcasting */
	class message final
	{
	public:
		message (btcb::websocket::topic topic_a) :
		topic (topic_a)
		{
		}
		message (btcb::websocket::topic topic_a, boost::property_tree::ptree & tree_a) :
		topic (topic_a), contents (tree_a)
		{
		}

		std::shared_ptr<std::string> to_string () const;
		btcb::websocket::topic topic;
		boost::property_tree::ptree contents;
	};

	/** Message builder. This is expanded with new builder functions are necessary. */
	class message_builder final
	{
	public:
		message block_confirmed (std::shared_ptr<btcb::block> block_a, btcb::account const & account_a, btcb::amount const & amount_a, std::string subtype);
		message vote_received (std::shared_ptr<btcb::vote> vote_a);

	private:
		/** Set the common fields for messages: timestamp and topic. */
		void set_common_fields (message & message_a);
	};

	/** Filtering options for subscriptions */
	class options
	{
	public:
		/**
		 * Checks if a message should be filtered for default options (no options given).
		 * @param message_a the message to be checked
		 * @return false - the message should always be broadcasted
		 */
		virtual bool should_filter (message const & message_a) const
		{
			return false;
		}
		virtual ~options () = default;
	};

	/**
	 * Filtering options for block confirmation subscriptions
	 * Possible filtering options:
	 * * "all_local_accounts" (bool) - will only not filter blocks that have local wallet accounts as source/destination
	 * * "accounts" (array of std::strings) - will only not filter blocks that have these accounts as source/destination
	 * @remark Both options can be given, the resulting filter is an intersection of individual filters
	 * @warn Legacy blocks are always filtered (not broadcasted)
	 */
	class confirmation_options final : public options
	{
	public:
		confirmation_options ();
		confirmation_options (boost::property_tree::ptree const & options_a, btcb::node & node_a);

		/**
		 * Checks if a message should be filtered for given block confirmation options.
		 * @param message_a the message to be checked
		 * @return false if the message should be broadcasted, true if it should be filtered
		 */
		bool should_filter (message const & message_a) const override;

	private:
		btcb::node & node;
		bool all_local_accounts{ false };
		std::unordered_set<std::string> accounts;
	};

	/**
	 * Filtering options for vote subscriptions
	 * Possible filtering options:
	 * * "representatives" (array of std::strings) - will only broadcast votes from these representatives
	 */
	class vote_options final : public options
	{
	public:
		vote_options ();
		vote_options (boost::property_tree::ptree const & options_a, btcb::node & node_a);

		/**
		 * Checks if a message should be filtered for given vote received options.
		 * @param message_a the message to be checked
		 * @return false if the message should be broadcasted, true if it should be filtered
		 */
		bool should_filter (message const & message_a) const override;

	private:
		btcb::node & node;
		std::unordered_set<std::string> representatives;
	};

	/** A websocket session managing its own lifetime */
	class session final : public std::enable_shared_from_this<session>
	{
	public:
		/** Constructor that takes ownership over \p socket_a */
		explicit session (btcb::websocket::listener & listener_a, socket_type socket_a);
		~session ();

		/** Perform Websocket handshake and start reading messages */
		void handshake ();

		/** Close the websocket and end the session */
		void close ();

		/** Read the next message. This implicitely handles incoming websocket pings. */
		void read ();

		/** Enqueue \p message_a for writing to the websockets */
		void write (btcb::websocket::message message_a);

	private:
		/** The owning listener */
		btcb::websocket::listener & ws_listener;
		/** Websocket */
		boost::beast::websocket::stream<socket_type> ws;
		/** Buffer for received messages */
		boost::beast::multi_buffer read_buffer;
		/** All websocket operations that are thread unsafe must go through a strand. */
		boost::asio::strand<boost::asio::io_context::executor_type> strand;
		/** Outgoing messages. The send queue is protected by accessing it only through the strand */
		std::deque<message> send_queue;

		/** Hash functor for topic enums */
		struct topic_hash
		{
			template <typename T>
			std::size_t operator() (T t) const
			{
				return static_cast<std::size_t> (t);
			}
		};
		/** Map of subscriptions -> options registered by this session. */
		std::unordered_map<topic, std::unique_ptr<options>, topic_hash> subscriptions;
		std::mutex subscriptions_mutex;

		/** Handle incoming message */
		void handle_message (boost::property_tree::ptree const & message_a);
		/** Acknowledge incoming message */
		void send_ack (std::string action_a, std::string id_a);
		/** Send all queued messages. This must be called from the write strand. */
		void write_queued_messages ();
	};

	/** Creates a new session for each incoming connection */
	class listener final : public std::enable_shared_from_this<listener>
	{
	public:
		listener (btcb::node & node_a, boost::asio::ip::tcp::endpoint endpoint_a);

		/** Start accepting connections */
		void run ();
		void accept ();
		void on_accept (boost::system::error_code ec_a);

		/** Close all websocket sessions and stop listening for new connections */
		void stop ();

		/** Broadcast \p message to all session subscribing to the message topic. */
		void broadcast (btcb::websocket::message message_a);

		btcb::node & get_node () const
		{
			return node;
		}

		/**
		 * Per-topic subscribers check. Relies on all sessions correctly increasing and
		 * decreasing the subscriber counts themselves.
		 */
		bool any_subscribers (btcb::websocket::topic const & topic_a);
		/** Adds to subscription count of a specific topic*/
		void increase_subscription_count (btcb::websocket::topic const & topic_a);
		/** Removes from subscription count of a specific topic*/
		void decrease_subscription_count (btcb::websocket::topic const & topic_a);

	private:
		btcb::node & node;
		boost::asio::ip::tcp::acceptor acceptor;
		socket_type socket;
		std::mutex sessions_mutex;
		std::vector<std::weak_ptr<session>> sessions;
		std::array<std::atomic<std::size_t>, number_topics> topic_subscription_count{};
		std::atomic<bool> stopped{ false };
	};
}
}
