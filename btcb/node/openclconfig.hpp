#pragma once

#include <btcb/lib/errors.hpp>
#include <btcb/lib/jsonconfig.hpp>

namespace btcb
{
class opencl_config
{
public:
	opencl_config () = default;
	opencl_config (unsigned, unsigned, unsigned);
	btcb::error serialize_json (btcb::jsonconfig &) const;
	btcb::error deserialize_json (btcb::jsonconfig &);
	unsigned platform{ 0 };
	unsigned device{ 0 };
	unsigned threads{ 1024 * 1024 };
};
}
