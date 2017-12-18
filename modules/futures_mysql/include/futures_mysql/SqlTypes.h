#pragma once

#include <futures/Core.h>

namespace futures {
namespace mysql {

using TinyType = signed char;
using ShortType = short int;
using LongType = int;
using LongLongType = long int;
using FloatType = float;
using DoubleType = double;
using StringType = std::string;
using BlobType = std::string;

struct NullType {
};

inline bool operator==(const NullType& l, const NullType &r) {
    return false;
}

inline bool operator!=(const NullType& l, const NullType &r) {
    return true;
}

static_assert(std::is_pod<NullType>::value, "NullType must be POD");

using CellDataType = Variant<
    NullType,
    TinyType, ShortType, LongType, LongLongType,
    FloatType, DoubleType,
    StringType, BlobType>;

}
}
