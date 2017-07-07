#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::Return;
using testing::_;

namespace Api {

MockApi::MockApi() { ON_CALL(*this, createFile(_, _, _, _)).WillByDefault(Return(file_)); }

MockApi::~MockApi() {}

} // namespace Api
} // namespace Envoy
