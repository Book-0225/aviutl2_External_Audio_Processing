#pragma once
#include <array>
#include <mutex>

struct ChainData
{
	float level = 0.0f;
	uint32_t update_count = 0;
};

class ChainManager
{
public:
	static const int32_t MAX_CHAINS = 16;
	static std::array<ChainData, MAX_CHAINS> chains;
	static std::mutex chains_mutex;
};