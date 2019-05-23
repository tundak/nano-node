#include <iostream>
#include <btcb/lib/utility.hpp>

namespace btcb
{
seq_con_info_composite::seq_con_info_composite (const std::string & name) :
name (name)
{
}

bool seq_con_info_composite::is_composite () const
{
	return true;
}

void seq_con_info_composite::add_component (std::unique_ptr<seq_con_info_component> child)
{
	children.push_back (std::move (child));
}

const std::vector<std::unique_ptr<seq_con_info_component>> & seq_con_info_composite::get_children () const
{
	return children;
}

const std::string & seq_con_info_composite::get_name () const
{
	return name;
}

seq_con_info_leaf::seq_con_info_leaf (const seq_con_info & info) :
info (info)
{
}
bool seq_con_info_leaf::is_composite () const
{
	return false;
}
const seq_con_info & seq_con_info_leaf::get_info () const
{
	return info;
}

namespace thread_role
{
	/*
	 * btcb::thread_role namespace
	 *
	 * Manage thread role
	 */
	static thread_local btcb::thread_role::name current_thread_role = btcb::thread_role::name::unknown;
	btcb::thread_role::name get ()
	{
		return current_thread_role;
	}

	std::string get_string (btcb::thread_role::name role)
	{
		std::string thread_role_name_string;

		switch (role)
		{
			case btcb::thread_role::name::unknown:
				thread_role_name_string = "<unknown>";
				break;
			case btcb::thread_role::name::io:
				thread_role_name_string = "I/O";
				break;
			case btcb::thread_role::name::work:
				thread_role_name_string = "Work pool";
				break;
			case btcb::thread_role::name::packet_processing:
				thread_role_name_string = "Pkt processing";
				break;
			case btcb::thread_role::name::alarm:
				thread_role_name_string = "Alarm";
				break;
			case btcb::thread_role::name::vote_processing:
				thread_role_name_string = "Vote processing";
				break;
			case btcb::thread_role::name::block_processing:
				thread_role_name_string = "Blck processing";
				break;
			case btcb::thread_role::name::request_loop:
				thread_role_name_string = "Request loop";
				break;
			case btcb::thread_role::name::wallet_actions:
				thread_role_name_string = "Wallet actions";
				break;
			case btcb::thread_role::name::work_watcher:
				thread_role_name_string = "Work watcher";
				break;
			case btcb::thread_role::name::bootstrap_initiator:
				thread_role_name_string = "Bootstrap init";
				break;
			case btcb::thread_role::name::voting:
				thread_role_name_string = "Voting";
				break;
			case btcb::thread_role::name::signature_checking:
				thread_role_name_string = "Signature check";
				break;
			case btcb::thread_role::name::rpc_request_processor:
				thread_role_name_string = "RPC processor";
				break;
			case btcb::thread_role::name::rpc_process_container:
				thread_role_name_string = "RPC process";
				break;
			case btcb::thread_role::name::confirmation_height_processing:
				thread_role_name_string = "Conf height";
				break;
		}

		/*
		 * We want to constrain the thread names to 15
		 * characters, since this is the smallest maximum
		 * length supported by the platforms we support
		 * (specifically, Linux)
		 */
		assert (thread_role_name_string.size () < 16);
		return (thread_role_name_string);
	}

	std::string get_string ()
	{
		return get_string (current_thread_role);
	}

	void set (btcb::thread_role::name role)
	{
		auto thread_role_name_string (get_string (role));

		btcb::thread_role::set_os_name (thread_role_name_string);

		btcb::thread_role::current_thread_role = role;
	}
}
}

void btcb::thread_attributes::set (boost::thread::attributes & attrs)
{
	auto attrs_l (&attrs);
	attrs_l->set_stack_size (8000000); //8MB
}

btcb::thread_runner::thread_runner (boost::asio::io_context & io_ctx_a, unsigned service_threads_a) :
io_guard (boost::asio::make_work_guard (io_ctx_a))
{
	boost::thread::attributes attrs;
	btcb::thread_attributes::set (attrs);
	for (auto i (0u); i < service_threads_a; ++i)
	{
		threads.push_back (boost::thread (attrs, [&io_ctx_a]() {
			btcb::thread_role::set (btcb::thread_role::name::io);
			try
			{
				io_ctx_a.run ();
			}
			catch (std::exception const & ex)
			{
				std::cerr << ex.what () << std::endl;
#ifndef NDEBUG
				throw ex;
#endif
			}
			catch (...)
			{
#ifndef NDEBUG
				/*
				 * In a release build, catch and swallow the
				 * io_context exception, in debug mode pass it
				 * on
				 */
				throw;
#endif
			}
		}));
	}
}

btcb::thread_runner::~thread_runner ()
{
	join ();
}

void btcb::thread_runner::join ()
{
	io_guard.reset ();
	for (auto & i : threads)
	{
		if (i.joinable ())
		{
			i.join ();
		}
	}
}

void btcb::thread_runner::stop_event_processing ()
{
	io_guard.get_executor ().context ().stop ();
}

/*
 * Backing code for "release_assert", which is itself a macro
 */
void release_assert_internal (bool check, const char * check_expr, const char * file, unsigned int line)
{
	if (check)
	{
		return;
	}

	std::cerr << "Assertion (" << check_expr << ") failed " << file << ":" << line << std::endl;
	abort ();
}
