#include "source/common/buffer/buffer_impl.h"

#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"
#include "contrib/sip_proxy/filters/network/test/mocks.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

TEST(AppExceptionImplTest, CopyConstructor) {
  AppException app_ex(AppExceptionType::InternalError, "msg");
  AppException copy(app_ex);

  EXPECT_EQ(app_ex.type_, copy.type_);
  EXPECT_STREQ("msg", copy.what());
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
