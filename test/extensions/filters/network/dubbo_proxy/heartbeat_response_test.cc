#include "source/extensions/filters/network/dubbo_proxy/heartbeat_response.h"
#include "source/extensions/filters/network/dubbo_proxy/hessian_utils.h"
#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"
#include "source/extensions/filters/network/dubbo_proxy/metadata.h"

#include "test/extensions/filters/network/dubbo_proxy/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

namespace {

class HeartbeatResponseTestWithMock : public testing::Test {
public:
  HeartbeatResponseTestWithMock() : metadata_(std::make_shared<MessageMetadata>()) {
    metadata_->setResponseStatus(ResponseStatus::Ok);
    metadata_->setMessageType(MessageType::HeartbeatResponse);
  }

  NiceMock<MockProtocol> mock_protocol_;
  MessageMetadataSharedPtr metadata_;
};

TEST_F(HeartbeatResponseTestWithMock, HeartbeatResponseTestWithMock) {
  {
    std::string mock_message("MOCK_MESSAGE");
    HeartbeatResponse heartbeat_response;

    Buffer::OwnedImpl buffer;

    ON_CALL(mock_protocol_, encode(_, _, _, _)).WillByDefault(Return(false));

    EXPECT_THROW_WITH_MESSAGE(heartbeat_response.encode(*metadata_, mock_protocol_, buffer),
                              EnvoyException, "failed to encode heartbeat message");
  }
  {
    std::string mock_message("MOCK_MESSAGE");
    HeartbeatResponse heartbeat_response;

    Buffer::OwnedImpl buffer;

    ON_CALL(mock_protocol_, encode(_, _, _, _)).WillByDefault(Return(true));

    EXPECT_EQ(heartbeat_response.encode(*metadata_, mock_protocol_, buffer),
              DubboFilters::DirectResponse::ResponseType::SuccessReply);
  }
}

} // namespace

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
