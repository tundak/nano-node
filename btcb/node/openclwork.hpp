#pragma once

#include <boost/optional.hpp>
#include <boost/property_tree/ptree.hpp>
#include <btcb/lib/errors.hpp>
#include <btcb/lib/jsonconfig.hpp>
#include <btcb/node/openclconfig.hpp>
#include <btcb/node/xorshift.hpp>

#include <map>
#include <mutex>
#include <vector>

#ifdef __APPLE__
#define CL_SILENCE_DEPRECATION
#include <OpenCL/opencl.h>
#else
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>
#endif

namespace btcb
{
class logger_mt;
class opencl_platform
{
public:
	cl_platform_id platform;
	std::vector<cl_device_id> devices;
};
class opencl_environment
{
public:
	opencl_environment (bool &);
	void dump (std::ostream & stream);
	std::vector<btcb::opencl_platform> platforms;
};
union uint256_union;
class work_pool;
class opencl_work
{
public:
	opencl_work (bool &, btcb::opencl_config const &, btcb::opencl_environment &, btcb::logger_mt &);
	~opencl_work ();
	boost::optional<uint64_t> generate_work (btcb::uint256_union const &, uint64_t const);
	static std::unique_ptr<opencl_work> create (bool, btcb::opencl_config const &, btcb::logger_mt &);
	btcb::opencl_config const & config;
	std::mutex mutex;
	cl_context context;
	cl_mem attempt_buffer;
	cl_mem result_buffer;
	cl_mem item_buffer;
	cl_mem difficulty_buffer;
	cl_program program;
	cl_kernel kernel;
	cl_command_queue queue;
	btcb::xorshift1024star rand;
	btcb::logger_mt & logger;
};
}
