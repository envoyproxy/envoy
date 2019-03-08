#include "mocks.h"

#include <cstdint>

#include "common/common/assert.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Common {
namespace Redis {

void PrintTo(const RespValue& value, std::ostream* os) { *os << value.toString(); }

void PrintTo(const RespValuePtr& value, std::ostream* os) { *os << value->toString(); }

bool operator==(const RespValue& lhs, const RespValue& rhs) {
  if (lhs.type() != rhs.type()) {
    return false;
  }

  switch (lhs.type()) {
  case RespType::Array: {
    if (lhs.asArray().size() != rhs.asArray().size()) {
      return false;
    }

    bool equal = true;
    for (uint64_t i = 0; i < lhs.asArray().size(); i++) {
      equal &= (lhs.asArray()[i] == rhs.asArray()[i]);
    }

    return equal;
  }
  case RespType::SimpleString:
  case RespType::BulkString:
  case RespType::Error: {
    return lhs.asString() == rhs.asString();
  }
  case RespType::Null: {
    return true;
  }
  case RespType::Integer: {
    return lhs.asInteger() == rhs.asInteger();
  }
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Redis
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
