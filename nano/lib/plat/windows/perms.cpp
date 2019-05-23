#include <boost/filesystem.hpp>
#include <cassert>
#include <btcb/lib/utility.hpp>

#include <io.h>
#include <processthreadsapi.h>
#include <sys/stat.h>
#include <sys/types.h>

void btcb::set_umask ()
{
	int oldMode;

	auto result (_umask_s (_S_IWRITE | _S_IREAD, &oldMode));
	assert (result == 0);
}

void btcb::set_secure_perm_directory (boost::filesystem::path const & path)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_all);
}

void btcb::set_secure_perm_directory (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_all, ec);
}

void btcb::set_secure_perm_file (boost::filesystem::path const & path)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_read | boost::filesystem::owner_write);
}

void btcb::set_secure_perm_file (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	boost::filesystem::permissions (path, boost::filesystem::owner_read | boost::filesystem::owner_write, ec);
}

bool btcb::is_windows_elevated ()
{
	bool is_elevated = false;
	HANDLE h_token = nullptr;
	if (OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &h_token))
	{
		TOKEN_ELEVATION elevation;
		DWORD cb_size = sizeof (TOKEN_ELEVATION);
		if (GetTokenInformation (h_token, TokenElevation, &elevation, sizeof (elevation), &cb_size))
		{
			is_elevated = elevation.TokenIsElevated;
		}
	}
	if (h_token)
	{
		CloseHandle (h_token);
	}
	return is_elevated;
}
