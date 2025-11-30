#include "ChainManager.h"

std::array<ChainData, ChainManager::MAX_CHAINS> ChainManager::chains;
std::mutex ChainManager::chains_mutex;