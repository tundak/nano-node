#pragma once

#include <btcb/lib/work.hpp>
#include <btcb/node/active_transactions.hpp>
#include <btcb/node/blockprocessor.hpp>
#include <btcb/node/bootstrap.hpp>
#include <btcb/node/confirmation_height_processor.hpp>
#include <btcb/node/logging.hpp>
#include <btcb/node/node_observers.hpp>
#include <btcb/node/nodeconfig.hpp>
#include <btcb/node/payment_observer_processor.hpp>
#include <btcb/node/portmapping.hpp>
#include <btcb/node/repcrawler.hpp>
#include <btcb/node/signatures.hpp>
#include <btcb/node/stats.hpp>
#include <btcb/node/transport/tcp.hpp>
#include <btcb/node/transport/udp.hpp>
#include <btcb/node/wallet.hpp>
#include <btcb/node/websocket.hpp>
#include <btcb/secure/ledger.hpp>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <queue>

#include <boost/asio/thread_pool.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/random_access_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/thread/thread.hpp>

namespace btcb
{
class channel;
class node;
class vote_info final
{
public:
	std::chrono::steady_clock::time_point time;
	uint64_t sequence;
	btcb::block_hash hash;
};
class election_vote_result final
{
public:
	election_vote_result () = default;
	election_vote_result (bool, bool);
	bool replay{ false };
	bool processed{ false };
};
class election final : public std::enable_shared_from_this<btcb::election>
{
	std::function<void(std::shared_ptr<btcb::block>)> confirmation_action;

public:
	election (btcb::node &, std::shared_ptr<btcb::block>, std::function<void(std::shared_ptr<btcb::block>)> const &);
	btcb::election_vote_result vote (btcb::account, uint64_t, btcb::block_hash);
	btcb::tally_t tally (btcb::transaction const &);
	// Check if we have vote quorum
	bool have_quorum (btcb::tally_t const &, btcb::uint128_t) const;
	// Change our winner to agree with the network
	void compute_rep_votes (btcb::transaction const &);
	void confirm_once ();
	// Confirm this block if quorum is met
	void confirm_if_quorum (btcb::transaction const &);
	void log_votes (btcb::tally_t const &) const;
	bool publish (std::shared_ptr<btcb::block> block_a);
	size_t last_votes_size ();
	void update_dependent ();
	void stop ();
	btcb::node & node;
	std::unordered_map<btcb::account, btcb::vote_info> last_votes;
	std::unordered_map<btcb::block_hash, std::shared_ptr<btcb::block>> blocks;
	std::chrono::steady_clock::time_point election_start;
	btcb::election_status status;
	std::atomic<bool> confirmed;
	bool stopped;
	std::unordered_map<btcb::block_hash, btcb::uint128_t> last_tally;
	unsigned announcements;
	std::unordered_set<btcb::block_hash> dependent_blocks;
};
class operation final
{
public:
	bool operator> (btcb::operation const &) const;
	std::chrono::steady_clock::time_point wakeup;
	std::function<void()> function;
};
class alarm final
{
public:
	explicit alarm (boost::asio::io_context &);
	~alarm ();
	void add (std::chrono::steady_clock::time_point const &, std::function<void()> const &);
	void run ();
	boost::asio::io_context & io_ctx;
	std::mutex mutex;
	std::condition_variable condition;
	std::priority_queue<operation, std::vector<operation>, std::greater<operation>> operations;
	boost::thread thread;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (alarm & alarm, const std::string & name);

class gap_information final
{
public:
	std::chrono::steady_clock::time_point arrival;
	btcb::block_hash hash;
	std::unordered_set<btcb::account> voters;
};
class gap_cache final
{
public:
	explicit gap_cache (btcb::node &);
	void add (btcb::transaction const &, btcb::block_hash const &, std::chrono::steady_clock::time_point = std::chrono::steady_clock::now ());
	void vote (std::shared_ptr<btcb::vote>);
	btcb::uint128_t bootstrap_threshold (btcb::transaction const &);
	size_t size ();
	boost::multi_index_container<
	btcb::gap_information,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<gap_information, std::chrono::steady_clock::time_point, &gap_information::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<gap_information, btcb::block_hash, &gap_information::hash>>>>
	blocks;
	size_t const max = 256;
	std::mutex mutex;
	btcb::node & node;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (gap_cache & gap_cache, const std::string & name);

class work_pool;
class block_arrival_info final
{
public:
	std::chrono::steady_clock::time_point arrival;
	btcb::block_hash hash;
};
// This class tracks blocks that are probably live because they arrived in a UDP packet
// This gives a fairly reliable way to differentiate between blocks being inserted via bootstrap or new, live blocks.
class block_arrival final
{
public:
	// Return `true' to indicated an error if the block has already been inserted
	bool add (btcb::block_hash const &);
	bool recent (btcb::block_hash const &);
	boost::multi_index_container<
	btcb::block_arrival_info,
	boost::multi_index::indexed_by<
	boost::multi_index::ordered_non_unique<boost::multi_index::member<btcb::block_arrival_info, std::chrono::steady_clock::time_point, &btcb::block_arrival_info::arrival>>,
	boost::multi_index::hashed_unique<boost::multi_index::member<btcb::block_arrival_info, btcb::block_hash, &btcb::block_arrival_info::hash>>>>
	arrival;
	std::mutex mutex;
	static size_t constexpr arrival_size_min = 8 * 1024;
	static std::chrono::seconds constexpr arrival_time_min = std::chrono::seconds (300);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_arrival & block_arrival, const std::string & name);

class online_reps final
{
public:
	online_reps (btcb::node &, btcb::uint128_t);
	void observe (btcb::account const &);
	void sample ();
	btcb::uint128_t online_stake () const;
	std::vector<btcb::account> list ();

private:
	btcb::uint128_t trend (btcb::transaction &);
	mutable std::mutex mutex;
	btcb::node & node;
	std::unordered_set<btcb::account> reps;
	btcb::uint128_t online;
	btcb::uint128_t minimum;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (online_reps & online_reps, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (online_reps & online_reps, const std::string & name);

class message_buffer final
{
public:
	uint8_t * buffer{ nullptr };
	size_t size{ 0 };
	btcb::endpoint endpoint;
};
/**
  * A circular buffer for servicing btcb realtime messages.
  * This container follows a producer/consumer model where the operating system is producing data in to
  * buffers which are serviced by internal threads.
  * If buffers are not serviced fast enough they're internally dropped.
  * This container has a maximum space to hold N buffers of M size and will allocate them in round-robin order.
  * All public methods are thread-safe
*/
class message_buffer_manager final
{
public:
	// Stats - Statistics
	// Size - Size of each individual buffer
	// Count - Number of buffers to allocate
	message_buffer_manager (btcb::stat & stats, size_t, size_t);
	// Return a buffer where message data can be put
	// Method will attempt to return the first free buffer
	// If there are no free buffers, an unserviced buffer will be dequeued and returned
	// Function will block if there are no free or unserviced buffers
	// Return nullptr if the container has stopped
	btcb::message_buffer * allocate ();
	// Queue a buffer that has been filled with message data and notify servicing threads
	void enqueue (btcb::message_buffer *);
	// Return a buffer that has been filled with message data
	// Function will block until a buffer has been added
	// Return nullptr if the container has stopped
	btcb::message_buffer * dequeue ();
	// Return a buffer to the freelist after is has been serviced
	void release (btcb::message_buffer *);
	// Stop container and notify waiting threads
	void stop ();

private:
	btcb::stat & stats;
	std::mutex mutex;
	std::condition_variable condition;
	boost::circular_buffer<btcb::message_buffer *> free;
	boost::circular_buffer<btcb::message_buffer *> full;
	std::vector<uint8_t> slab;
	std::vector<btcb::message_buffer> entries;
	bool stopped;
};
class network final
{
public:
	network (btcb::node &, uint16_t);
	~network ();
	void start ();
	void stop ();
	void flood_message (btcb::message const &);
	void flood_vote (std::shared_ptr<btcb::vote> vote_a)
	{
		btcb::confirm_ack message (vote_a);
		flood_message (message);
	}
	void flood_block (std::shared_ptr<btcb::block> block_a)
	{
		btcb::publish publish (block_a);
		flood_message (publish);
	}
	void flood_block_batch (std::deque<std::shared_ptr<btcb::block>>, unsigned = broadcast_interval_ms);
	void merge_peers (std::array<btcb::endpoint, 8> const &);
	void merge_peer (btcb::endpoint const &);
	void send_keepalive (std::shared_ptr<btcb::transport::channel>);
	void send_keepalive_self (std::shared_ptr<btcb::transport::channel>);
	void send_node_id_handshake (std::shared_ptr<btcb::transport::channel>, boost::optional<btcb::uint256_union> const & query, boost::optional<btcb::uint256_union> const & respond_to);
	void broadcast_confirm_req (std::shared_ptr<btcb::block>);
	void broadcast_confirm_req_base (std::shared_ptr<btcb::block>, std::shared_ptr<std::vector<std::shared_ptr<btcb::transport::channel>>>, unsigned, bool = false);
	void broadcast_confirm_req_batch (std::unordered_map<std::shared_ptr<btcb::transport::channel>, std::vector<std::pair<btcb::block_hash, btcb::block_hash>>>, unsigned = broadcast_interval_ms, bool = false);
	void broadcast_confirm_req_batch (std::deque<std::pair<std::shared_ptr<btcb::block>, std::shared_ptr<std::vector<std::shared_ptr<btcb::transport::channel>>>>>, unsigned = broadcast_interval_ms);
	void confirm_hashes (btcb::transaction const &, std::shared_ptr<btcb::transport::channel>, std::vector<btcb::block_hash>);
	bool send_votes_cache (std::shared_ptr<btcb::transport::channel>, btcb::block_hash const &);
	std::shared_ptr<btcb::transport::channel> find_node_id (btcb::account const &);
	std::shared_ptr<btcb::transport::channel> find_channel (btcb::endpoint const &);
	bool not_a_peer (btcb::endpoint const &, bool);
	// Should we reach out to this endpoint with a keepalive message
	bool reachout (btcb::endpoint const &, bool = false);
	std::deque<std::shared_ptr<btcb::transport::channel>> list (size_t);
	// A list of random peers sized for the configured rebroadcast fanout
	std::deque<std::shared_ptr<btcb::transport::channel>> list_fanout ();
	void random_fill (std::array<btcb::endpoint, 8> &) const;
	std::unordered_set<std::shared_ptr<btcb::transport::channel>> random_set (size_t) const;
	// Get the next peer for attempting a tcp bootstrap connection
	btcb::tcp_endpoint bootstrap_peer ();
	// Response channels
	void add_response_channels (btcb::tcp_endpoint const &, std::vector<btcb::tcp_endpoint>);
	std::shared_ptr<btcb::transport::channel> search_response_channel (btcb::tcp_endpoint const &, btcb::account const &);
	void remove_response_channel (btcb::tcp_endpoint const &);
	size_t response_channels_size ();
	btcb::endpoint endpoint ();
	void cleanup (std::chrono::steady_clock::time_point const &);
	void ongoing_cleanup ();
	size_t size () const;
	size_t size_sqrt () const;
	bool empty () const;
	btcb::message_buffer_manager buffer_container;
	boost::asio::ip::udp::resolver resolver;
	std::vector<boost::thread> packet_processing_threads;
	btcb::node & node;
	btcb::transport::udp_channels udp_channels;
	btcb::transport::tcp_channels tcp_channels;
	std::function<void()> disconnect_observer;
	// Called when a new channel is observed
	std::function<void(std::shared_ptr<btcb::transport::channel>)> channel_observer;
	static unsigned const broadcast_interval_ms = 10;
	static size_t const buffer_size = 512;
	static size_t const confirm_req_hashes_max = 6;

private:
	std::mutex response_channels_mutex;
	std::unordered_map<btcb::tcp_endpoint, std::vector<btcb::tcp_endpoint>> response_channels;
};

class node_init final
{
public:
	bool error () const;
	bool block_store_init{ false };
	bool wallets_store_init{ false };
};

class vote_processor final
{
public:
	explicit vote_processor (btcb::node &);
	void vote (std::shared_ptr<btcb::vote>, std::shared_ptr<btcb::transport::channel>);
	// node.active.mutex lock required
	btcb::vote_code vote_blocking (btcb::transaction const &, std::shared_ptr<btcb::vote>, std::shared_ptr<btcb::transport::channel>, bool = false);
	void verify_votes (std::deque<std::pair<std::shared_ptr<btcb::vote>, std::shared_ptr<btcb::transport::channel>>> &);
	void flush ();
	void calculate_weights ();
	btcb::node & node;
	void stop ();

private:
	void process_loop ();
	std::deque<std::pair<std::shared_ptr<btcb::vote>, std::shared_ptr<btcb::transport::channel>>> votes;
	// Representatives levels for random early detection
	std::unordered_set<btcb::account> representatives_1;
	std::unordered_set<btcb::account> representatives_2;
	std::unordered_set<btcb::account> representatives_3;
	std::condition_variable condition;
	std::mutex mutex;
	bool started;
	bool stopped;
	bool active;
	boost::thread thread;

	friend std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_processor & vote_processor, const std::string & name);
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (vote_processor & vote_processor, const std::string & name);
std::unique_ptr<seq_con_info_component> collect_seq_con_info (rep_crawler & rep_crawler, const std::string & name);
std::unique_ptr<seq_con_info_component> collect_seq_con_info (block_processor & block_processor, const std::string & name);

class node final : public std::enable_shared_from_this<btcb::node>
{
public:
	node (btcb::node_init &, boost::asio::io_context &, uint16_t, boost::filesystem::path const &, btcb::alarm &, btcb::logging const &, btcb::work_pool &);
	node (btcb::node_init &, boost::asio::io_context &, boost::filesystem::path const &, btcb::alarm &, btcb::node_config const &, btcb::work_pool &, btcb::node_flags = btcb::node_flags (), bool delay_frontier_confirmation_height_updating = false);
	~node ();
	template <typename T>
	void background (T action_a)
	{
		alarm.io_ctx.post (action_a);
	}
	bool copy_with_compaction (boost::filesystem::path const &);
	void keepalive (std::string const &, uint16_t);
	void start ();
	void stop ();
	std::shared_ptr<btcb::node> shared ();
	int store_version ();
	void receive_confirmed (btcb::transaction const &, std::shared_ptr<btcb::block>, btcb::block_hash const &);
	void process_confirmed (std::shared_ptr<btcb::block>, uint8_t = 0);
	void process_message (btcb::message const &, std::shared_ptr<btcb::transport::channel>);
	void process_active (std::shared_ptr<btcb::block>);
	btcb::process_return process (btcb::block const &);
	void keepalive_preconfigured (std::vector<std::string> const &);
	btcb::block_hash latest (btcb::account const &);
	btcb::uint128_t balance (btcb::account const &);
	std::shared_ptr<btcb::block> block (btcb::block_hash const &);
	std::pair<btcb::uint128_t, btcb::uint128_t> balance_pending (btcb::account const &);
	btcb::uint128_t weight (btcb::account const &);
	btcb::account representative (btcb::account const &);
	void ongoing_rep_calculation ();
	void ongoing_bootstrap ();
	void ongoing_store_flush ();
	void ongoing_peer_store ();
	void ongoing_unchecked_cleanup ();
	void backup_wallet ();
	void search_pending ();
	void bootstrap_wallet ();
	void unchecked_cleanup ();
	int price (btcb::uint128_t const &, int);
	void work_generate_blocking (btcb::block &, uint64_t);
	void work_generate_blocking (btcb::block &);
	uint64_t work_generate_blocking (btcb::uint256_union const &, uint64_t);
	uint64_t work_generate_blocking (btcb::uint256_union const &);
	void work_generate (btcb::uint256_union const &, std::function<void(uint64_t)>, uint64_t);
	void work_generate (btcb::uint256_union const &, std::function<void(uint64_t)>);
	void add_initial_peers ();
	void block_confirm (std::shared_ptr<btcb::block>);
	bool block_confirmed_or_being_confirmed (btcb::transaction const &, btcb::block_hash const &);
	void process_fork (btcb::transaction const &, std::shared_ptr<btcb::block>);
	bool validate_block_by_previous (btcb::transaction const &, std::shared_ptr<btcb::block>);
	void do_rpc_callback (boost::asio::ip::tcp::resolver::iterator i_a, std::string const &, uint16_t, std::shared_ptr<std::string>, std::shared_ptr<std::string>, std::shared_ptr<boost::asio::ip::tcp::resolver>);
	btcb::uint128_t delta () const;
	void ongoing_online_weight_calculation ();
	void ongoing_online_weight_calculation_queue ();
	bool online () const;
	boost::asio::io_context & io_ctx;
	btcb::network_params network_params;
	btcb::node_config config;
	std::shared_ptr<btcb::websocket::listener> websocket_server;
	btcb::node_flags flags;
	btcb::alarm & alarm;
	btcb::work_pool & work;
	btcb::logger_mt logger;
	std::unique_ptr<btcb::block_store> store_impl;
	btcb::block_store & store;
	std::unique_ptr<btcb::wallets_store> wallets_store_impl;
	btcb::wallets_store & wallets_store;
	btcb::gap_cache gap_cache;
	btcb::ledger ledger;
	btcb::signature_checker checker;
	btcb::network network;
	btcb::bootstrap_initiator bootstrap_initiator;
	btcb::bootstrap_listener bootstrap;
	boost::filesystem::path application_path;
	btcb::node_observers observers;
	btcb::port_mapping port_mapping;
	btcb::vote_processor vote_processor;
	btcb::rep_crawler rep_crawler;
	unsigned warmed_up;
	btcb::block_processor block_processor;
	boost::thread block_processor_thread;
	btcb::block_arrival block_arrival;
	btcb::online_reps online_reps;
	btcb::wallets wallets;
	btcb::votes_cache votes_cache;
	btcb::stat stats;
	btcb::keypair node_id;
	btcb::block_uniquer block_uniquer;
	btcb::vote_uniquer vote_uniquer;
	btcb::pending_confirmation_height pending_confirmation_height; // Used by both active and confirmation height processor
	btcb::active_transactions active;
	btcb::confirmation_height_processor confirmation_height_processor;
	btcb::payment_observer_processor payment_observer_processor;
	const std::chrono::steady_clock::time_point startup_time;
	std::chrono::seconds unchecked_cutoff = std::chrono::seconds (7 * 24 * 60 * 60); // Week
	static double constexpr price_max = 16.0;
	static double constexpr free_cutoff = 1024.0;
};

std::unique_ptr<seq_con_info_component> collect_seq_con_info (node & node, const std::string & name);

class inactive_node final
{
public:
	inactive_node (boost::filesystem::path const & path = btcb::working_path (), uint16_t = 24000);
	~inactive_node ();
	boost::filesystem::path path;
	std::shared_ptr<boost::asio::io_context> io_context;
	btcb::alarm alarm;
	btcb::logging logging;
	btcb::node_init init;
	btcb::work_pool work;
	uint16_t peering_port;
	std::shared_ptr<btcb::node> node;
};
}
