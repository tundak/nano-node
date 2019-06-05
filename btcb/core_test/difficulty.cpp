#include <gtest/gtest.h>

#include <btcb/lib/numbers.hpp>

TEST (difficulty, multipliers)
{
	{
		uint64_t base = 0xff00000000000000;
		uint64_t difficulty = 0xfff27e7a57c285cd;
		double expected_multiplier = 18.95461493377003;

		ASSERT_NEAR (expected_multiplier, btcb::difficulty::to_multiplier (difficulty, base), 1e-10);
		ASSERT_EQ (difficulty, btcb::difficulty::from_multiplier (expected_multiplier, base));
	}

	{
		uint64_t base = 0xffffffc000000000;
		uint64_t difficulty = 0xfffffe0000000000;
		double expected_multiplier = 0.125;

		ASSERT_NEAR (expected_multiplier, btcb::difficulty::to_multiplier (difficulty, base), 1e-10);
		ASSERT_EQ (difficulty, btcb::difficulty::from_multiplier (expected_multiplier, base));
	}

	{
		uint64_t base = 0xffffffc000000000;
		uint64_t difficulty_nil = 0;
		double multiplier_nil = 0.;
#ifndef NDEBUG
		ASSERT_DEATH_IF_SUPPORTED (btcb::difficulty::to_multiplier (difficulty_nil, base), "");
		ASSERT_DEATH_IF_SUPPORTED (btcb::difficulty::from_multiplier (multiplier_nil, base), "");
#endif
	}
}
