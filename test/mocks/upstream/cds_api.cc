#include "cds_api.h"

#include <functional>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
using ::testing::_;
using ::testing::SaveArg;
MockCdsApi::MockCdsApi() {
  ON_CALL(*this, setInitializedCb(_)).WillByDefault(SaveArg<0>(&initialized_callback_));
}

MockCdsApi::~MockCdsApi() = default;

} // namespace Upstream
} // namespace Envoy
