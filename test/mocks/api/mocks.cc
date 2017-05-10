#include "mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Lyft {
using testing::_;
using testing::Return;

namespace Api {

MockApi::MockApi() { ON_CALL(*this, createFile(_, _, _, _)).WillByDefault(Return(file_)); }

MockApi::~MockApi() {}

} // Api
} // Lyft