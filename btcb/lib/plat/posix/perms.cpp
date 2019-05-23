#include <boost/filesystem.hpp>

#include <btcb/lib/utility.hpp>

#include <sys/stat.h>
#include <sys/types.h>

void btcb::set_umask ()
{
	umask (077);
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
	boost::filesystem::permissions (path, boost::filesystem::perms::owner_read | boost::filesystem::perms::owner_write);
}

void btcb::set_secure_perm_file (boost::filesystem::path const & path, boost::system::error_code & ec)
{
	boost::filesystem::permissions (path, boost::filesystem::perms::owner_read | boost::filesystem::perms::owner_write, ec);
}
