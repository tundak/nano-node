#include <btcb/crypto_lib/random_pool.hpp>

std::mutex btcb::random_pool::mutex;
CryptoPP::AutoSeededRandomPool btcb::random_pool::pool;

void btcb::random_pool::generate_block (unsigned char * output, size_t size)
{
	std::lock_guard<std::mutex> lk (mutex);
	pool.GenerateBlock (output, size);
}

unsigned btcb::random_pool::generate_word32 (unsigned min, unsigned max)
{
	std::lock_guard<std::mutex> lk (mutex);
	return pool.GenerateWord32 (min, max);
}

unsigned char btcb::random_pool::generate_byte ()
{
	std::lock_guard<std::mutex> lk (mutex);
	return pool.GenerateByte ();
}
