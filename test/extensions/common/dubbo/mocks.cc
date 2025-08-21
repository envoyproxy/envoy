#include "test/extensions/common/dubbo/mocks.h"

#include <memory>

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

MockSerializer::MockSerializer() { ON_CALL(*this, type()).WillByDefault(Return(type_)); }
MockSerializer::~MockSerializer() = default;

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
