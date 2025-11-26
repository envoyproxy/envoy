#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/common/reverse_connection_utility.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseConnectionUtilityTest : public testing::Test {
protected:
  ReverseConnectionUtilityTest() = default;
};

// Test isPingMessage functionality
TEST_F(ReverseConnectionUtilityTest, IsPingMessageEmptyData) {
  // Test with empty data
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage(""));
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage(absl::string_view()));
}

TEST_F(ReverseConnectionUtilityTest, IsPingMessageExactMatch) {
  // Test with exact RPING match
  EXPECT_TRUE(ReverseConnectionUtility::isPingMessage("RPING"));
  EXPECT_TRUE(ReverseConnectionUtility::isPingMessage(absl::string_view("RPING")));
}

TEST_F(ReverseConnectionUtilityTest, IsPingMessageInvalidData) {
  // Test with non-RPING data. isPingMessage should return false for these cases.
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage("PING"));
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage("RPIN"));
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage("RPINGG"));
  EXPECT_FALSE(ReverseConnectionUtility::isPingMessage("Hello World"));
}

// Test createPingResponse functionality
TEST_F(ReverseConnectionUtilityTest, CreatePingResponse) {
  auto ping_buffer = ReverseConnectionUtility::createPingResponse();

  EXPECT_NE(ping_buffer, nullptr);
  EXPECT_EQ(ping_buffer->toString(), "RPING");
  EXPECT_EQ(ping_buffer->length(), 5);
}

// Test sendPingResponse with Connection
TEST_F(ReverseConnectionUtilityTest, SendPingResponseConnection) {
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Set up mock expectations
  EXPECT_CALL(*connection, write(_, false));
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = ReverseConnectionUtility::sendPingResponse(*connection);

  EXPECT_TRUE(result);
}

// Test sendPingResponse with IoHandle
TEST_F(ReverseConnectionUtilityTest, SendPingResponseIoHandleSuccess) {
  auto io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();

  EXPECT_CALL(*io_handle, write(_))
      .WillOnce(Return(Api::IoCallUint64Result{5, Api::IoError::none()}));

  Api::IoCallUint64Result result = ReverseConnectionUtility::sendPingResponse(*io_handle);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.return_value_, 5);
  EXPECT_EQ(result.err_, nullptr);
}

TEST_F(ReverseConnectionUtilityTest, SendPingResponseIoHandleFailure) {
  auto io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();

  // Set up mock expectations for failed write
  EXPECT_CALL(*io_handle, write(_))
      .WillOnce(Return(Api::IoCallUint64Result{0, Network::IoSocketError::create(ECONNRESET)}));

  Api::IoCallUint64Result result = ReverseConnectionUtility::sendPingResponse(*io_handle);

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.return_value_, 0);
  EXPECT_NE(result.err_, nullptr);
}

// Test handlePingMessage functionality
TEST_F(ReverseConnectionUtilityTest, HandlePingMessageValidPing) {
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // should call sendPingResponse and return true since it is a valid RPING message
  EXPECT_CALL(*connection, write(_, false));
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = ReverseConnectionUtility::handlePingMessage("RPING", *connection);

  EXPECT_TRUE(result);
}

TEST_F(ReverseConnectionUtilityTest, HandlePingMessageInvalidData) {
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Should not call sendPingResponse for invalid data
  EXPECT_CALL(*connection, write(_, _)).Times(0);
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = ReverseConnectionUtility::handlePingMessage("INVALID", *connection);

  EXPECT_FALSE(result);
}

TEST_F(ReverseConnectionUtilityTest, HandlePingMessageEmptyData) {
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Should not call sendPingResponse for empty data
  EXPECT_CALL(*connection, write(_, _)).Times(0);
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = ReverseConnectionUtility::handlePingMessage("", *connection);

  EXPECT_FALSE(result);
}

// Test extractPingFromHttpData functionality
TEST_F(ReverseConnectionUtilityTest, ExtractPingFromHttpDataValid) {
  // Test with RPING in HTTP response body
  EXPECT_TRUE(ReverseConnectionUtility::extractPingFromHttpData("HTTP/1.1 200 OK\r\n\r\nRPING"));
  EXPECT_TRUE(ReverseConnectionUtility::extractPingFromHttpData(
      "GET /ping HTTP/1.1\r\nHost: example.com\r\n\r\nRPING"));
  EXPECT_TRUE(ReverseConnectionUtility::extractPingFromHttpData(
      "POST /data HTTP/1.1\r\nContent-Length: 5\r\n\r\nRPING"));
}

TEST_F(ReverseConnectionUtilityTest, ExtractPingFromHttpDataInvalid) {
  // Test with no RPING in HTTP data
  EXPECT_FALSE(ReverseConnectionUtility::extractPingFromHttpData("HTTP/1.1 200 OK\r\n\r\nHello"));
  EXPECT_FALSE(ReverseConnectionUtility::extractPingFromHttpData(
      "GET /ping HTTP/1.1\r\nHost: example.com\r\n\r\nPING"));
  EXPECT_FALSE(ReverseConnectionUtility::extractPingFromHttpData(""));
}

// Test ReverseConnectionMessageHandlerFactory functionality
TEST_F(ReverseConnectionUtilityTest, CreatePingHandler) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();

  EXPECT_NE(handler, nullptr);
  EXPECT_EQ(handler->getPingCount(), 0);
}

// Test PingMessageHandler functionality
TEST_F(ReverseConnectionUtilityTest, PingMessageHandlerProcessValidPing) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Set up mock expectations
  EXPECT_CALL(*connection, write(_, false));
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = handler->processPingMessage("RPING", *connection);

  EXPECT_TRUE(result);
  EXPECT_EQ(handler->getPingCount(), 1);
}

TEST_F(ReverseConnectionUtilityTest, PingMessageHandlerProcessInvalidPing) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Set up mock expectations - should not call write for invalid data
  EXPECT_CALL(*connection, write(_, _)).Times(0);
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = handler->processPingMessage("INVALID", *connection);

  EXPECT_FALSE(result);
  EXPECT_EQ(handler->getPingCount(), 0);
}

TEST_F(ReverseConnectionUtilityTest, PingMessageHandlerProcessMultiplePings) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Set up mock expectations for multiple writes
  EXPECT_CALL(*connection, write(_, false)).Times(3);
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  // Process multiple valid pings
  EXPECT_TRUE(handler->processPingMessage("RPING", *connection));
  EXPECT_TRUE(handler->processPingMessage("RPING", *connection));
  EXPECT_TRUE(handler->processPingMessage("RPING", *connection));

  EXPECT_EQ(handler->getPingCount(), 3);
}

TEST_F(ReverseConnectionUtilityTest, PingMessageHandlerProcessEmptyPing) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();

  // Set up mock expectations - should not call write for empty data
  EXPECT_CALL(*connection, write(_, _)).Times(0);
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  bool result = handler->processPingMessage("", *connection);

  EXPECT_FALSE(result);
  EXPECT_EQ(handler->getPingCount(), 0);
}

TEST_F(ReverseConnectionUtilityTest, PingMessageHandlerGetPingCount) {
  auto handler = ReverseConnectionMessageHandlerFactory::createPingHandler();

  // Initially should be 0
  EXPECT_EQ(handler->getPingCount(), 0);

  // After processing a ping, should be 1
  auto connection = std::make_unique<NiceMock<Network::MockConnection>>();
  EXPECT_CALL(*connection, write(_, false));
  EXPECT_CALL(*connection, id()).WillRepeatedly(Return(12345));

  handler->processPingMessage("RPING", *connection);
  EXPECT_EQ(handler->getPingCount(), 1);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
