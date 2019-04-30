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
namespace Multiplexing {

MockRouter::MockRouter() {}
MockRouter::~MockRouter() {}

namespace ConnPool {

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

} // namespace ConnPool
} // namespace Multiplexing
} // namespace Common
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
