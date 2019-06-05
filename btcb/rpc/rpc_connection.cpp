#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>
#include <btcb/lib/config.hpp>
#include <btcb/lib/json_error_response.hpp>
#include <btcb/lib/logger_mt.hpp>
#include <btcb/lib/rpc_handler_interface.hpp>
#include <btcb/lib/rpcconfig.hpp>
#include <btcb/rpc/rpc_connection.hpp>
#include <btcb/rpc/rpc_handler.hpp>

btcb::rpc_connection::rpc_connection (btcb::rpc_config const & rpc_config, boost::asio::io_context & io_ctx, btcb::logger_mt & logger, btcb::rpc_handler_interface & rpc_handler_interface) :
socket (io_ctx),
io_ctx (io_ctx),
logger (logger),
rpc_config (rpc_config),
rpc_handler_interface (rpc_handler_interface)
{
	responded.clear ();
}

void btcb::rpc_connection::parse_connection ()
{
	read ();
}

void btcb::rpc_connection::prepare_head (unsigned version, boost::beast::http::status status)
{
	res.version (version);
	res.result (status);
	res.set (boost::beast::http::field::allow, "POST, OPTIONS");
	res.set (boost::beast::http::field::content_type, "application/json");
	res.set (boost::beast::http::field::access_control_allow_origin, "*");
	res.set (boost::beast::http::field::access_control_allow_methods, "POST, OPTIONS");
	res.set (boost::beast::http::field::access_control_allow_headers, "Accept, Accept-Language, Content-Language, Content-Type");
	res.set (boost::beast::http::field::connection, "close");
}

void btcb::rpc_connection::write_result (std::string body, unsigned version, boost::beast::http::status status)
{
	if (!responded.test_and_set ())
	{
		prepare_head (version, status);
		res.body () = body;
		res.prepare_payload ();
	}
	else
	{
		assert (false && "RPC already responded and should only respond once");
		// Guards `res' from being clobbered while async_write is being serviced
	}
}

void btcb::rpc_connection::read ()
{
	auto this_l (shared_from_this ());
	auto header_parser (std::make_shared<boost::beast::http::request_parser<boost::beast::http::empty_body>> ());
	header_parser->body_limit (rpc_config.max_request_size);
	boost::beast::http::async_read_header (socket, buffer, *header_parser, [this_l, header_parser](boost::system::error_code const & ec, size_t bytes_transferred) {
		if (!ec)
		{
			if (boost::iequals (header_parser->get ()[boost::beast::http::field::expect], "100-continue"))
			{
				auto continue_response (std::make_shared<boost::beast::http::response<boost::beast::http::empty_body>> ());
				continue_response->version (11);
				continue_response->result (boost::beast::http::status::continue_);
				continue_response->set (boost::beast::http::field::server, "btcb");
				boost::beast::http::async_write (this_l->socket, *continue_response, [this_l, continue_response](boost::system::error_code const & ec, size_t bytes_transferred) {});
			}

			this_l->parse_request (header_parser);
		}
		else
		{
			this_l->logger.always_log ("RPC header error: ", ec.message ());

			// Respond with the reason for the invalid header
			auto response_handler ([this_l](std::string const & tree_a) {
				this_l->write_result (tree_a, 11);
				boost::beast::http::async_write (this_l->socket, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
					this_l->write_completion_handler (this_l);
				});
			});
			json_error_response (response_handler, std::string ("Invalid header: ") + ec.message ());
		}
	});
}

void btcb::rpc_connection::parse_request (std::shared_ptr<boost::beast::http::request_parser<boost::beast::http::empty_body>> header_parser)
{
	auto this_l (shared_from_this ());
	auto body_parser (std::make_shared<boost::beast::http::request_parser<boost::beast::http::string_body>> (std::move (*header_parser)));
	boost::beast::http::async_read (socket, buffer, *body_parser, [this_l, body_parser](boost::system::error_code const & ec, size_t bytes_transferred) {
		if (!ec)
		{
			// equivalent to background
			this_l->io_ctx.post ([this_l, body_parser]() {
				auto & req (body_parser->get ());
				auto start (std::chrono::steady_clock::now ());
				auto version (req.version ());
				std::string request_id (boost::str (boost::format ("%1%") % boost::io::group (std::hex, std::showbase, reinterpret_cast<uintptr_t> (this_l.get ()))));
				auto response_handler ([this_l, version, start, request_id](std::string const & tree_a) {
					auto body = tree_a;
					this_l->write_result (body, version);
					boost::beast::http::async_write (this_l->socket, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
						this_l->write_completion_handler (this_l);
					});

					this_l->logger.always_log (boost::str (boost::format ("RPC request %2% completed in: %1% microseconds") % std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now () - start).count () % request_id));
				});
				auto method = req.method ();
				switch (method)
				{
					case boost::beast::http::verb::post:
					{
						auto handler (std::make_shared<btcb::rpc_handler> (this_l->rpc_config, req.body (), request_id, response_handler, this_l->rpc_handler_interface, this_l->logger));
						handler->process_request ();
						break;
					}
					case boost::beast::http::verb::options:
					{
						this_l->prepare_head (version);
						this_l->res.prepare_payload ();
						boost::beast::http::async_write (this_l->socket, this_l->res, [this_l](boost::system::error_code const & ec, size_t bytes_transferred) {
							this_l->write_completion_handler (this_l);
						});
						break;
					}
					default:
					{
						json_error_response (response_handler, "Can only POST requests");
						break;
					}
				}
			});
		}
		else
		{
			this_l->logger.always_log ("RPC read error: ", ec.message ());
		}
	});
}

void btcb::rpc_connection::write_completion_handler (std::shared_ptr<btcb::rpc_connection> rpc_connection)
{
	// Intentional no-op
}
