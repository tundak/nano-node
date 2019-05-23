#include <boost/filesystem.hpp>

namespace btcb
{
class node_flags;
}
namespace btcb_daemon
{
class daemon
{
public:
	void run (boost::filesystem::path const &, btcb::node_flags const & flags);
};
}
