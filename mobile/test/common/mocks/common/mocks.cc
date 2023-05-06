#include "test/common/mocks/common/mocks.h"

namespace Envoy {
namespace test {

using testing::_;

MockSystemHelper::MockSystemHelper() {
  ON_CALL(*this, isCleartextPermitted(_)).WillByDefault(testing::Return(true));
}

} // namespace test
} // namespace Envoy
