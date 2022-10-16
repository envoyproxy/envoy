#include "envoy/common/optref.h"

#include "source/extensions/common/wasm/context.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class TestContext : public Context {
public:
  void setEncoderFilterCallbacksPtr(Envoy::Http::StreamEncoderFilterCallbacks* cb) {
    encoder_callbacks_ = cb;
  }

  void setDecoderFilterCallbacksPtr(Envoy::Http::StreamDecoderFilterCallbacks* cb) {
    decoder_callbacks_ = cb;
  }

  void setAccessLogStreamInfoPtr(StreamInfo::StreamInfo* stream_info) {
    access_log_stream_info_ = stream_info;
  };

  void setNetworkReadFilterCallbacksPtr(Network::ReadFilterCallbacks* cb) {
    network_read_filter_callbacks_ = cb;
  }

  void setNetworkWriteFilterCallbacksPtr(Network::WriteFilterCallbacks* cb) {
    network_write_filter_callbacks_ = cb;
  }
};

class ContextTest : public testing::Test {
protected:
  TestContext ctx;
};

// Tests the stub functions which are added just for inheritance,
// in order to resolve coverage issue.
// If a function listed in this test has a specific implementation,
// it have to be moved to the other test and have a own test case.
TEST_F(ContextTest, StubFunctionsTest) {
  EXPECT_EQ(ctx.FindFunctionOverloads("function").size(), 0);
}

// getConstRequestStreamInfo and getRequestStreamInfo should return
// the stream info from encoder_callbacks_, decoder_callbacks_,
// access_log_stream_info_, network_read_filter_callbacks_, or
// network_write_filter_callbacks_ after checking if each one is
// null or not in the given sequence.
TEST_F(ContextTest, GetRequestStreamInfoTest) {
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks;
  Envoy::StreamInfo::MockStreamInfo encoder_si;
  EXPECT_CALL(encoder_callbacks, streamInfo())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(encoder_si));
  ctx.setEncoderFilterCallbacksPtr(&encoder_callbacks);

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  Envoy::StreamInfo::MockStreamInfo decoder_si;
  EXPECT_CALL(decoder_callbacks, streamInfo())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(decoder_si));
  ctx.setDecoderFilterCallbacksPtr(&decoder_callbacks);

  Envoy::StreamInfo::MockStreamInfo access_log_si;
  ctx.setAccessLogStreamInfoPtr(&access_log_si);

  Network::MockReadFilterCallbacks read_filter_callbacks;
  Envoy::Network::MockConnection read_filter_connection;
  Envoy::StreamInfo::MockStreamInfo read_filter_si;
  EXPECT_CALL(read_filter_callbacks, connection())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(read_filter_connection));
  EXPECT_CALL(read_filter_connection, streamInfo())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(read_filter_si));
  ctx.setNetworkReadFilterCallbacksPtr(&read_filter_callbacks);

  Network::MockWriteFilterCallbacks write_filter_callbacks;
  Envoy::Network::MockConnection write_filter_connection;
  Envoy::StreamInfo::MockStreamInfo write_filter_si;
  EXPECT_CALL(write_filter_callbacks, connection())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(write_filter_connection));
  EXPECT_CALL(write_filter_connection, streamInfo())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(write_filter_si));
  ctx.setNetworkWriteFilterCallbacksPtr(&write_filter_callbacks);

  EXPECT_EQ(ctx.getConstRequestStreamInfo(), &encoder_si);
  EXPECT_EQ(ctx.getRequestStreamInfo(), &encoder_si);
  ctx.setEncoderFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx.getConstRequestStreamInfo(), &decoder_si);
  EXPECT_EQ(ctx.getRequestStreamInfo(), &decoder_si);
  ctx.setDecoderFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx.getConstRequestStreamInfo(), &access_log_si);
  ctx.setAccessLogStreamInfoPtr(nullptr);

  EXPECT_EQ(ctx.getConstRequestStreamInfo(), &read_filter_si);
  EXPECT_EQ(ctx.getRequestStreamInfo(), &read_filter_si);
  ctx.setNetworkReadFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx.getConstRequestStreamInfo(), &write_filter_si);
  EXPECT_EQ(ctx.getRequestStreamInfo(), &write_filter_si);
  ctx.setNetworkWriteFilterCallbacksPtr(nullptr);
}

TEST_F(ContextTest, GetConnectionTest) {
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks;
  Envoy::Network::MockConnection encoder_connection;
  EXPECT_CALL(encoder_callbacks, connection())
      .Times(1)
      .WillRepeatedly(testing::Return(
          makeOptRef(dynamic_cast<const Network::Connection&>(encoder_connection))));
  ctx.setEncoderFilterCallbacksPtr(&encoder_callbacks);

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  Envoy::Network::MockConnection decoder_connection;
  EXPECT_CALL(decoder_callbacks, connection())
      .Times(1)
      .WillRepeatedly(testing::Return(
          makeOptRef(dynamic_cast<const Network::Connection&>(decoder_connection))));
  ctx.setDecoderFilterCallbacksPtr(&decoder_callbacks);

  Envoy::StreamInfo::MockStreamInfo access_log_si;
  ctx.setAccessLogStreamInfoPtr(&access_log_si);

  Network::MockReadFilterCallbacks read_filter_callbacks;
  Envoy::Network::MockConnection read_filter_connection;
  EXPECT_CALL(read_filter_callbacks, connection())
      .Times(1)
      .WillRepeatedly(testing::ReturnRef(read_filter_connection));
  ctx.setNetworkReadFilterCallbacksPtr(&read_filter_callbacks);

  Network::MockWriteFilterCallbacks write_filter_callbacks;
  Envoy::Network::MockConnection write_filter_connection;
  EXPECT_CALL(write_filter_callbacks, connection())
      .Times(1)
      .WillRepeatedly(testing::ReturnRef(write_filter_connection));
  ctx.setNetworkWriteFilterCallbacksPtr(&write_filter_callbacks);

  EXPECT_EQ(ctx.getConnection(), &encoder_connection);
  ctx.setEncoderFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx.getConnection(), &decoder_connection);
  ctx.setDecoderFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx.getConnection(), &read_filter_connection);
  ctx.setNetworkReadFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx.getConnection(), &write_filter_connection);
  ctx.setNetworkWriteFilterCallbacksPtr(nullptr);
}

using google::api::expr::runtime::CelList;
using google::api::expr::runtime::CelMap;

class MockCelMap : public CelMap {
public:
  MOCK_METHOD(absl::StatusOr<const CelList*>, ListKeys, (), (const, override));
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
