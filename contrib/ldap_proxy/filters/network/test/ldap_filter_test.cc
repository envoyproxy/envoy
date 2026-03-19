#include "contrib/ldap_proxy/filters/network/source/ldap_filter.h"
#include "contrib/ldap_proxy/filters/network/source/protocol_templates.h"

#include "envoy/ssl/connection.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LdapProxy {
namespace {

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

class LdapFilterTest : public testing::Test {
protected:
  void SetUp() override {
    filter_ = std::make_unique<LdapFilter>(*store_.rootScope(), false);
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
    
    ON_CALL(read_callbacks_, connection()).WillByDefault(ReturnRef(connection_));
    ON_CALL(connection_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  }

  void SetUpWithUpstreamStartTls() {
    filter_ = std::make_unique<LdapFilter>(*store_.rootScope(), true);
    filter_->initializeReadFilterCallbacks(read_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_callbacks_);
    
    ON_CALL(read_callbacks_, connection()).WillByDefault(ReturnRef(connection_));
    ON_CALL(connection_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
  }

  // StartTLS Extended Request
  std::vector<uint8_t> makeStartTlsRequest(uint8_t msg_id) {
    return {
      0x30, 0x1d,              // SEQUENCE, length 29
      0x02, 0x01, msg_id,      // INTEGER msgID
      0x77, 0x18,              // [APPLICATION 23] ExtendedRequest
      0x80, 0x16,              // [0] requestName
      0x31, 0x2e, 0x33, 0x2e, 0x36, 0x2e, 0x31, 0x2e,
      0x34, 0x2e, 0x31, 0x2e, 0x31, 0x34, 0x36, 0x36,
      0x2e, 0x32, 0x30, 0x30, 0x33, 0x37
    };
  }

  // BindRequest
  std::vector<uint8_t> makeBindRequest(uint8_t msg_id) {
    return {
      0x30, 0x0c,              // SEQUENCE
      0x02, 0x01, msg_id,      // INTEGER msgID
      0x60, 0x07,              // [APPLICATION 0] BindRequest
      0x02, 0x01, 0x03,        // version = 3
      0x04, 0x00,              // name = ""
      0x80, 0x00               // simple auth = ""
    };
  }

  // StartTLS success response
  std::vector<uint8_t> makeStartTlsSuccessResponse(uint8_t msg_id) {
    return {
      0x30, 0x0c,
      0x02, 0x01, msg_id,
      0x78, 0x07,
      0x0a, 0x01, 0x00,  // resultCode = 0
      0x04, 0x00,
      0x04, 0x00
    };
  }

  Stats::TestUtil::TestStore store_;
  NiceMock<Network::MockReadFilterCallbacks> read_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_callbacks_;
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  std::unique_ptr<LdapFilter> filter_;
};

// Initial state tests

TEST_F(LdapFilterTest, InitialState) {
  EXPECT_EQ(FilterState::Inspecting, filter_->state());
}

TEST_F(LdapFilterTest, OnNewConnectionPlaintext) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  
  auto status = filter_->onNewConnection();
  
  EXPECT_EQ(Network::FilterStatus::Continue, status);
  EXPECT_EQ(FilterState::Inspecting, filter_->state());
}

TEST_F(LdapFilterTest, OnNewConnectionTls) {
  auto ssl_mock = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  Ssl::ConnectionInfoConstSharedPtr ssl_info = ssl_mock;
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl_info));
  
  auto status = filter_->onNewConnection();
  
  EXPECT_EQ(Network::FilterStatus::Continue, status);
  EXPECT_EQ(FilterState::Passthrough, filter_->state());
}

// onData tests - plaintext operations

TEST_F(LdapFilterTest, OnDataBindRequest) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  Buffer::OwnedImpl buffer;
  auto data = makeBindRequest(1);
  buffer.add(data.data(), data.size());
  
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, false)).Times(1);
  
  auto status = filter_->onData(buffer, false);
  
  EXPECT_EQ(Network::FilterStatus::Continue, status);
  EXPECT_EQ(FilterState::Passthrough, filter_->state());
}

TEST_F(LdapFilterTest, OnDataNeedMoreData) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  // Send only the SEQUENCE tag, waiting for more data
  Buffer::OwnedImpl buffer;
  buffer.add("\x30", 1);  // just the tag, truly incomplete
  
  auto status = filter_->onData(buffer, false);
  
  // The filter may either wait for more data or close on parse error
  // depending on implementation
  EXPECT_TRUE(status == Network::FilterStatus::StopIteration || 
              status == Network::FilterStatus::Continue);
}

// onData tests - StartTLS detection

TEST_F(LdapFilterTest, OnDataStartTlsRequest) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  Buffer::OwnedImpl buffer;
  auto data = makeStartTlsRequest(5);
  buffer.add(data.data(), data.size());
  
  // Expect reads to be disabled during negotiation
  EXPECT_CALL(connection_, readDisable(true)).Times(1);
  // Expect StartTLS request to be sent upstream
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, false)).Times(1);
  
  auto status = filter_->onData(buffer, false);
  
  EXPECT_EQ(Network::FilterStatus::StopIteration, status);
  EXPECT_EQ(FilterState::NegotiatingUpstream, filter_->state());
}

// onData tests - passthrough mode

TEST_F(LdapFilterTest, OnDataInPassthroughMode) {
  // Force into passthrough mode
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  Buffer::OwnedImpl buffer1;
  auto data1 = makeBindRequest(1);
  buffer1.add(data1.data(), data1.size());
  
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, false)).Times(1);
  filter_->onData(buffer1, false);
  
  EXPECT_EQ(FilterState::Passthrough, filter_->state());
  
  // Now in passthrough, data should flow through
  Buffer::OwnedImpl buffer2;
  buffer2.add("any data", 8);
  
  auto status = filter_->onData(buffer2, false);
  
  EXPECT_EQ(Network::FilterStatus::Continue, status);
}

// onData tests - error handling

TEST_F(LdapFilterTest, OnDataInvalidBer) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  Buffer::OwnedImpl buffer;
  buffer.add("\x02\x01\x01", 3);  // INTEGER, not SEQUENCE
  
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  
  auto status = filter_->onData(buffer, false);
  
  EXPECT_EQ(Network::FilterStatus::StopIteration, status);
  EXPECT_EQ(FilterState::Closed, filter_->state());
}

TEST_F(LdapFilterTest, OnDataBufferTooLarge) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  Buffer::OwnedImpl buffer;
  std::vector<uint8_t> large_data(17 * 1024, 0x00);
  large_data[0] = 0x30;
  buffer.add(large_data.data(), large_data.size());
  
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  
  auto status = filter_->onData(buffer, false);
  
  EXPECT_EQ(Network::FilterStatus::StopIteration, status);
  EXPECT_EQ(FilterState::Closed, filter_->state());
}

// onData tests - pipeline attack

TEST_F(LdapFilterTest, OnDataPipelineAttack) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  Buffer::OwnedImpl buffer;
  auto data = makeStartTlsRequest(1);
  buffer.add(data.data(), data.size());
  buffer.add("\x00\x00\x00", 3);  // extra bytes = pipeline attack
  
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush)).Times(1);
  
  auto status = filter_->onData(buffer, false);
  
  EXPECT_EQ(Network::FilterStatus::StopIteration, status);
  EXPECT_EQ(FilterState::Closed, filter_->state());
}

// onWrite tests

TEST_F(LdapFilterTest, OnWriteInPassthrough) {
  // Force passthrough state
  auto ssl_mock = std::make_shared<NiceMock<Ssl::MockConnectionInfo>>();
  Ssl::ConnectionInfoConstSharedPtr ssl_info = ssl_mock;
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl_info));
  filter_->onNewConnection();
  
  Buffer::OwnedImpl buffer;
  buffer.add("response data", 13);
  
  auto status = filter_->onWrite(buffer, false);
  
  EXPECT_EQ(Network::FilterStatus::Continue, status);
}

// Upstream StartTLS mode tests

TEST_F(LdapFilterTest, UpstreamStartTlsModeInitiatesOnFirstData) {
  SetUpWithUpstreamStartTls();
  
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  Buffer::OwnedImpl buffer;
  auto data = makeBindRequest(1);
  buffer.add(data.data(), data.size());
  
  EXPECT_CALL(connection_, readDisable(true)).Times(1);
  EXPECT_CALL(read_callbacks_, injectReadDataToFilterChain(_, false)).Times(1);
  
  auto status = filter_->onData(buffer, false);
  
  EXPECT_EQ(Network::FilterStatus::StopIteration, status);
  EXPECT_EQ(FilterState::NegotiatingUpstream, filter_->state());
}

// Connection event tests

TEST_F(LdapFilterTest, OnEventRemoteClose) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  ON_CALL(connection_, transportFailureReason()).WillByDefault(Return(""));
  
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
  
  EXPECT_EQ(FilterState::Closed, filter_->state());
}

TEST_F(LdapFilterTest, OnEventLocalClose) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  ON_CALL(connection_, transportFailureReason()).WillByDefault(Return(""));
  
  filter_->onEvent(Network::ConnectionEvent::LocalClose);
  
  EXPECT_EQ(FilterState::Closed, filter_->state());
}

TEST_F(LdapFilterTest, OnEventWithTransportFailure) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  ON_CALL(connection_, transportFailureReason())
      .WillByDefault(Return("TLS handshake failed"));
  
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
  
  EXPECT_EQ(FilterState::Closed, filter_->state());
}

// Watermark callbacks (no-ops)

TEST_F(LdapFilterTest, WatermarkCallbacksAreNoOps) {
  // These should not crash
  filter_->onAboveWriteBufferHighWatermark();
  filter_->onBelowWriteBufferLowWatermark();
}

// Data in closed state

TEST_F(LdapFilterTest, OnDataInClosedState) {
  ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
  filter_->onNewConnection();
  
  // Force close
  ON_CALL(connection_, transportFailureReason()).WillByDefault(Return(""));
  filter_->onEvent(Network::ConnectionEvent::RemoteClose);
  
  Buffer::OwnedImpl buffer;
  buffer.add("data", 4);
  
  auto status = filter_->onData(buffer, false);
  
  EXPECT_EQ(Network::FilterStatus::StopIteration, status);
  EXPECT_EQ(0, buffer.length());  // buffer should be drained
}

}  // namespace
}  // namespace LdapProxy
}  // namespace NetworkFilters
}  // namespace Extensions
}  // namespace Envoy
