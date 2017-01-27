#pragma once

#include <cassert>

#define CHECK_EQ(x, y) assert((x) == (y))
#define DCHECK_EQ(x, y) CHECK_EQ(x, y)
#define DCHECK(x) assert(x)
#define CHECK(x) assert(x)
#define DCHECK_LT(x, y) assert((x) < (y))
#define DCHECK_GE(x, y) assert((x) >= (y))

