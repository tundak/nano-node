#include <btcb/lib/config.hpp>
#include <btcb/lib/interface.h>
#include <btcb/node/logging.hpp>
#include <btcb/node/working.hpp>
#include <btcb/secure/utility.hpp>

static std::vector<boost::filesystem::path> all_unique_paths;

boost::filesystem::path btcb::working_path (bool legacy)
{
	static btcb::network_constants network_constants;
	auto result (btcb::app_path ());
	switch (network_constants.network ())
	{
		case btcb::btcb_networks::btcb_test_network:
			if (!legacy)
			{
				result /= "BtcbTest";
			}
			else
			{
				result /= "RaiBlocksTest";
			}
			break;
		case btcb::btcb_networks::btcb_beta_network:
			if (!legacy)
			{
				result /= "BtcbBeta";
			}
			else
			{
				result /= "RaiBlocksBeta";
			}
			break;
		case btcb::btcb_networks::btcb_live_network:
			if (!legacy)
			{
				result /= "Btcb";
			}
			else
			{
				result /= "RaiBlocks";
			}
			break;
	}
	return result;
}

bool btcb::migrate_working_path (std::string & error_string)
{
	bool result (true);
	auto old_path (btcb::working_path (true));
	auto new_path (btcb::working_path ());

	if (old_path != new_path)
	{
		boost::system::error_code status_error;

		auto old_path_status (boost::filesystem::status (old_path, status_error));
		if (status_error == boost::system::errc::success && boost::filesystem::exists (old_path_status) && boost::filesystem::is_directory (old_path_status))
		{
			auto new_path_status (boost::filesystem::status (new_path, status_error));
			if (!boost::filesystem::exists (new_path_status))
			{
				boost::system::error_code rename_error;

				boost::filesystem::rename (old_path, new_path, rename_error);
				if (rename_error != boost::system::errc::success)
				{
					std::stringstream error_string_stream;

					error_string_stream << "Unable to migrate data from " << old_path << " to " << new_path;

					error_string = error_string_stream.str ();

					result = false;
				}
			}
		}
	}

	return result;
}

boost::filesystem::path btcb::unique_path ()
{
	auto result (working_path () / boost::filesystem::unique_path ());
	all_unique_paths.push_back (result);
	return result;
}

void btcb::remove_temporary_directories ()
{
	for (auto & path : all_unique_paths)
	{
		boost::system::error_code ec;
		boost::filesystem::remove_all (path, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary directory: " << ec.message () << std::endl;
		}

		// lmdb creates a -lock suffixed file for its MDB_NOSUBDIR databases
		auto lockfile = path;
		lockfile += "-lock";
		boost::filesystem::remove (lockfile, ec);
		if (ec)
		{
			std::cerr << "Could not remove temporary lock file: " << ec.message () << std::endl;
		}
	}
}
