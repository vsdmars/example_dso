#pragma once
#include "cache.h"

using namespace LRUC;

using Cache = LRUCache<int, int>;

Cache& getCache() noexcept;

