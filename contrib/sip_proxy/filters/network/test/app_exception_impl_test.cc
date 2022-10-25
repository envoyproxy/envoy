#include "source/common/buffer/buffer_impl.h"

#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"
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

TEST(AppExceptionImplTest, EncodeWithoutNecessaryHeaders) {
  AppException app_ex(AppExceptionType::InternalError, "msg");
  MessageMetadata metadata;
  Buffer::OwnedImpl buffer;
  app_ex.encode(metadata, buffer);
}

TEST(AppExceptionImplTest, EncodeWithNecessaryHeaders) {
  AppException app_ex(AppExceptionType::InternalError, "msg");
  MessageMetadata metadata;
  metadata.setEP("10.0.0.1");
  metadata.addNewMsgHeader(HeaderType::To, "<sip:User.0000@tas01.defult.svc.cluster.local>");
  metadata.addNewMsgHeader(HeaderType::CallId, "1-3193@11.0.0.10");
  metadata.addNewMsgHeader(HeaderType::Via, "SIP/2.0/TCP 11.0.0.10:15060;");
  metadata.addNewMsgHeader(HeaderType::Cseq, "1 INVITE");
  Buffer::OwnedImpl buffer;
  app_ex.encode(metadata, buffer);
}

TEST(AppExceptionImplTest, EncodeCustomCodeWithoutNecessaryHeaders) {
  AppException app_ex(AppExceptionType::InternalError, ErrorCode::ServiceUnavailable, "msg");
  MessageMetadata metadata;
  Buffer::OwnedImpl buffer;
  app_ex.encode(metadata, buffer);
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
