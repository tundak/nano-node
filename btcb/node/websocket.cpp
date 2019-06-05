#include <algorithm>
#include <boost/property_tree/json_parser.hpp>
#include <chrono>
#include <btcb/node/node.hpp>
#include <btcb/node/websocket.hpp>

btcb::websocket::confirmation_options::confirmation_options (boost::property_tree::ptree const & options_a, btcb::node & node_a) :
node (node_a),
all_local_accounts (options_a.get<bool> ("all_local_accounts", false))
{
	auto accounts_l (options_a.get_child_optional ("accounts"));
	if (accounts_l)
	{
		for (auto account_l : *accounts_l)
		{
			btcb::account result_l (0);
			if (!result_l.decode_account (account_l.second.data ()))
			{
				// Do not insert the given raw data to keep old prefix support
				accounts.insert (result_l.to_account ());
			}
			else
			{
				node.logger.always_log (boost::str (boost::format ("Websocket: invalid account provided for filtering blocks: %1%") % account_l.second.data ()));
			}
		}
	}
	// Warn the user if the options resulted in an empty filter
	if (!all_local_accounts && accounts.empty ())
	{
		node.logger.always_log ("Websocket: provided options resulted in an empty block confirmation filter");
	}
}

bool btcb::websocket::confirmation_options::should_filter (btcb::websocket::message const & message_a) const
{
	bool should_filter_l (true);
	auto destination_opt_l (message_a.contents.get_optional<std::string> ("message.block.link_as_account"));
	if (destination_opt_l)
	{
		auto source_text_l (message_a.contents.get<std::string> ("message.account"));
		if (all_local_accounts)
		{
			auto transaction_l (node.wallets.tx_begin_read ());
			btcb::account source_l (0), destination_l (0);
			auto decode_source_ok_l (!source_l.decode_account (source_text_l));
			auto decode_destination_ok_l (!destination_l.decode_account (destination_opt_l.get ()));
			assert (decode_source_ok_l && decode_destination_ok_l);
			if (node.wallets.exists (transaction_l, source_l) || node.wallets.exists (transaction_l, destination_l))
			{
				should_filter_l = false;
			}
		}
		if (accounts.find (source_text_l) != accounts.end () || accounts.find (destination_opt_l.get ()) != accounts.end ())
		{
			should_filter_l = false;
		}
	}
	return should_filter_l;
}

btcb::websocket::vote_options::vote_options (boost::property_tree::ptree const & options_a, btcb::node & node_a) :
node (node_a)
{
	auto representatives_l (options_a.get_child_optional ("representatives"));
	if (representatives_l)
	{
		for (auto representative_l : *representatives_l)
		{
			btcb::account result_l (0);
			if (!result_l.decode_account (representative_l.second.data ()))
			{
				// Do not insert the given raw data to keep old prefix support
				representatives.insert (result_l.to_account ());
			}
			else
			{
				node.logger.always_log (boost::str (boost::format ("Websocket: invalid account given to filter votes: %1%") % representative_l.second.data ()));
			}
		}
	}
	// Warn the user if the options resulted in an empty filter
	if (representatives.empty ())
	{
		node.logger.always_log ("Websocket: provided options resulted in an empty vote filter");
	}
}

bool btcb::websocket::vote_options::should_filter (btcb::websocket::message const & message_a) const
{
	bool should_filter_l (true);
	auto representative_text_l (message_a.contents.get<std::string> ("message.account"));
	if (representatives.find (representative_text_l) != representatives.end ())
	{
		should_filter_l = false;
	}
	return should_filter_l;
}

btcb::websocket::session::session (btcb::websocket::listener & listener_a, socket_type socket_a) :
ws_listener (listener_a), ws (std::move (socket_a)), strand (ws.get_executor ())
{
	ws.text (true);
	ws_listener.get_node ().logger.try_log ("Websocket: session started");
}

btcb::websocket::session::~session ()
{
	{
		std::unique_lock<std::mutex> lk (subscriptions_mutex);
		for (auto & subscription : subscriptions)
		{
			ws_listener.decrease_subscription_count (subscription.first);
		}
	}
}

void btcb::websocket::session::handshake ()
{
	auto this_l (shared_from_this ());
	ws.async_accept ([this_l](boost::system::error_code const & ec) {
		if (!ec)
		{
			// Start reading incoming messages
			this_l->read ();
		}
		else
		{
			this_l->ws_listener.get_node ().logger.always_log ("Websocket: handshake failed: ", ec.message ());
		}
	});
}

void btcb::websocket::session::close ()
{
	ws_listener.get_node ().logger.try_log ("Websocket: session closing");

	auto this_l (shared_from_this ());
	// clang-format off
	boost::asio::dispatch (strand,
	[this_l]() {
		boost::beast::websocket::close_reason reason;
		reason.code = boost::beast::websocket::close_code::normal;
		reason.reason = "Shutting down";
		boost::system::error_code ec_ignore;
		this_l->ws.close (reason, ec_ignore);
	});
	// clang-format on
}

void btcb::websocket::session::write (btcb::websocket::message message_a)
{
	// clang-format off
	std::unique_lock<std::mutex> lk (subscriptions_mutex);
	auto subscription (subscriptions.find (message_a.topic));
	if (message_a.topic == btcb::websocket::topic::ack || (subscription != subscriptions.end () && !subscription->second->should_filter (message_a)))
	{
		lk.unlock ();
		auto this_l (shared_from_this ());
		boost::asio::post (strand,
		[message_a, this_l]() {
			bool write_in_progress = !this_l->send_queue.empty ();
			this_l->send_queue.emplace_back (message_a);
			if (!write_in_progress)
			{
				this_l->write_queued_messages ();
			}
		});
	}
	// clang-format on
}

void btcb::websocket::session::write_queued_messages ()
{
	auto msg (send_queue.front ());
	auto msg_str (msg.to_string ());
	auto this_l (shared_from_this ());

	// clang-format off
	ws.async_write (boost::asio::buffer (msg_str->data (), msg_str->size ()),
	boost::asio::bind_executor (strand,
	[msg_str, this_l](boost::system::error_code ec, std::size_t bytes_transferred) {
		this_l->send_queue.pop_front ();
		if (!ec)
		{
			if (!this_l->send_queue.empty ())
			{
				this_l->write_queued_messages ();
			}
		}
	}));
	// clang-format on
}

void btcb::websocket::session::read ()
{
	auto this_l (shared_from_this ());

	// clang-format off
	boost::asio::post (strand, [this_l]() {
		this_l->ws.async_read (this_l->read_buffer,
		boost::asio::bind_executor (this_l->strand,
		[this_l](boost::system::error_code ec, std::size_t bytes_transferred) {
			if (!ec)
			{
				std::stringstream os;
				os << beast_buffers (this_l->read_buffer.data ());
				std::string incoming_message = os.str ();

				// Prepare next read by clearing the multibuffer
				this_l->read_buffer.consume (this_l->read_buffer.size ());

				boost::property_tree::ptree tree_msg;
				try
				{
					boost::property_tree::read_json (os, tree_msg);
					this_l->handle_message (tree_msg);
					this_l->read ();
				}
				catch (boost::property_tree::json_parser::json_parser_error const & ex)
				{
					this_l->ws_listener.get_node ().logger.try_log ("Websocket: json parsing failed: ", ex.what ());
				}
			}
			else
			{
				this_l->ws_listener.get_node ().logger.try_log ("Websocket: read failed: ", ec.message ());
			}
		}));
	});
	// clang-format on
}

namespace
{
btcb::websocket::topic to_topic (std::string topic_a)
{
	btcb::websocket::topic topic = btcb::websocket::topic::invalid;
	if (topic_a == "confirmation")
	{
		topic = btcb::websocket::topic::confirmation;
	}
	else if (topic_a == "vote")
	{
		topic = btcb::websocket::topic::vote;
	}
	else if (topic_a == "ack")
	{
		topic = btcb::websocket::topic::ack;
	}
	return topic;
}

std::string from_topic (btcb::websocket::topic topic_a)
{
	std::string topic = "invalid";
	if (topic_a == btcb::websocket::topic::confirmation)
	{
		topic = "confirmation";
	}
	else if (topic_a == btcb::websocket::topic::vote)
	{
		topic = "vote";
	}
	else if (topic_a == btcb::websocket::topic::ack)
	{
		topic = "ack";
	}
	return topic;
}
}

void btcb::websocket::session::send_ack (std::string action_a, std::string id_a)
{
	auto milli_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
	btcb::websocket::message msg (btcb::websocket::topic::ack);
	boost::property_tree::ptree & message_l = msg.contents;
	message_l.add ("ack", action_a);
	message_l.add ("time", std::to_string (milli_since_epoch));
	if (!id_a.empty ())
	{
		message_l.add ("id", id_a);
	}
	write (msg);
}

void btcb::websocket::session::handle_message (boost::property_tree::ptree const & message_a)
{
	std::string action (message_a.get<std::string> ("action", ""));
	auto topic_l (to_topic (message_a.get<std::string> ("topic", "")));
	auto ack_l (message_a.get<bool> ("ack", false));
	auto id_l (message_a.get<std::string> ("id", ""));
	auto action_succeeded (false);
	if (action == "subscribe" && topic_l != btcb::websocket::topic::invalid)
	{
		auto options_l (message_a.get_child_optional ("options"));
		std::lock_guard<std::mutex> lk (subscriptions_mutex);
		if (topic_l == btcb::websocket::topic::confirmation)
		{
			subscriptions.insert (std::make_pair (topic_l, options_l ? std::make_unique<btcb::websocket::confirmation_options> (options_l.get (), ws_listener.get_node ()) : std::make_unique<btcb::websocket::options> ()));
		}
		else if (topic_l == btcb::websocket::topic::vote)
		{
			subscriptions.insert (std::make_pair (topic_l, options_l ? std::make_unique<btcb::websocket::vote_options> (options_l.get (), ws_listener.get_node ()) : std::make_unique<btcb::websocket::options> ()));
		}
		else
		{
			subscriptions.insert (std::make_pair (topic_l, std::make_unique<btcb::websocket::options> ()));
		}
		ws_listener.increase_subscription_count (topic_l);
		action_succeeded = true;
	}
	else if (action == "unsubscribe" && topic_l != btcb::websocket::topic::invalid)
	{
		std::lock_guard<std::mutex> lk (subscriptions_mutex);
		if (subscriptions.erase (topic_l))
		{
			ws_listener.decrease_subscription_count (topic_l);
		}
		action_succeeded = true;
	}
	if (ack_l && action_succeeded)
	{
		send_ack (action, id_l);
	}
}

void btcb::websocket::listener::stop ()
{
	stopped = true;
	acceptor.close ();

	std::lock_guard<std::mutex> lk (sessions_mutex);
	for (auto & weak_session : sessions)
	{
		auto session_ptr (weak_session.lock ());
		if (session_ptr)
		{
			session_ptr->close ();
		}
	}
	sessions.clear ();
}

btcb::websocket::listener::listener (btcb::node & node_a, boost::asio::ip::tcp::endpoint endpoint_a) :
node (node_a),
acceptor (node_a.io_ctx),
socket (node_a.io_ctx)
{
	try
	{
		acceptor.open (endpoint_a.protocol ());
		acceptor.set_option (boost::asio::socket_base::reuse_address (true));
		acceptor.bind (endpoint_a);
		acceptor.listen (boost::asio::socket_base::max_listen_connections);
	}
	catch (std::exception const & ex)
	{
		node.logger.always_log ("Websocket: listen failed: ", ex.what ());
	}
}

void btcb::websocket::listener::run ()
{
	if (acceptor.is_open ())
	{
		accept ();
	}
}

void btcb::websocket::listener::accept ()
{
	auto this_l (shared_from_this ());
	acceptor.async_accept (socket,
	[this_l](boost::system::error_code const & ec) {
		this_l->on_accept (ec);
	});
}

void btcb::websocket::listener::on_accept (boost::system::error_code ec)
{
	if (ec)
	{
		node.logger.always_log ("Websocket: accept failed: ", ec.message ());
	}
	else
	{
		// Create the session and initiate websocket handshake
		auto session (std::make_shared<btcb::websocket::session> (*this, std::move (socket)));
		sessions_mutex.lock ();
		sessions.push_back (session);
		// Clean up expired sessions
		sessions.erase (std::remove_if (sessions.begin (), sessions.end (), [](auto & elem) { return elem.expired (); }), sessions.end ());
		sessions_mutex.unlock ();
		session->handshake ();
	}

	if (!stopped)
	{
		accept ();
	}
}

void btcb::websocket::listener::broadcast (btcb::websocket::message message_a)
{
	std::lock_guard<std::mutex> lk (sessions_mutex);
	for (auto & weak_session : sessions)
	{
		auto session_ptr (weak_session.lock ());
		if (session_ptr)
		{
			session_ptr->write (message_a);
		}
	}
}

bool btcb::websocket::listener::any_subscribers (btcb::websocket::topic const & topic_a)
{
	return topic_subscription_count[static_cast<std::size_t> (topic_a)] > 0;
}

void btcb::websocket::listener::increase_subscription_count (btcb::websocket::topic const & topic_a)
{
	topic_subscription_count[static_cast<std::size_t> (topic_a)] += 1;
}

void btcb::websocket::listener::decrease_subscription_count (btcb::websocket::topic const & topic_a)
{
	auto & count (topic_subscription_count[static_cast<std::size_t> (topic_a)]);
	release_assert (count > 0);
	count -= 1;
}

btcb::websocket::message btcb::websocket::message_builder::block_confirmed (std::shared_ptr<btcb::block> block_a, btcb::account const & account_a, btcb::amount const & amount_a, std::string subtype)
{
	btcb::websocket::message message_l (btcb::websocket::topic::confirmation);
	set_common_fields (message_l);

	// Block confirmation properties
	boost::property_tree::ptree message_node_l;
	message_node_l.add ("account", account_a.to_account ());
	message_node_l.add ("amount", amount_a.to_string_dec ());
	message_node_l.add ("hash", block_a->hash ().to_string ());
	boost::property_tree::ptree block_node_l;
	block_a->serialize_json (block_node_l);
	if (!subtype.empty ())
	{
		block_node_l.add ("subtype", subtype);
	}
	message_node_l.add_child ("block", block_node_l);
	message_l.contents.add_child ("message", message_node_l);

	return message_l;
}

btcb::websocket::message btcb::websocket::message_builder::vote_received (std::shared_ptr<btcb::vote> vote_a)
{
	btcb::websocket::message message_l (btcb::websocket::topic::vote);
	set_common_fields (message_l);

	// Vote information
	boost::property_tree::ptree vote_node_l;
	vote_a->serialize_json (vote_node_l);
	message_l.contents.add_child ("message", vote_node_l);
	return message_l;
}

void btcb::websocket::message_builder::set_common_fields (btcb::websocket::message & message_a)
{
	using namespace std::chrono;
	auto milli_since_epoch = std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();

	// Common message information
	message_a.contents.add ("topic", from_topic (message_a.topic));
	message_a.contents.add ("time", std::to_string (milli_since_epoch));
}

std::shared_ptr<std::string> btcb::websocket::message::to_string () const
{
	std::ostringstream ostream;
	boost::property_tree::write_json (ostream, contents);
	ostream.flush ();
	return std::make_shared<std::string> (ostream.str ());
}
