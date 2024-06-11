#include "envoy/common/optref.h"

#include "source/extensions/common/wasm/context.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"

#include "eval/public/cel_value.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

using google::api::expr::runtime::CelList;
using google::api::expr::runtime::CelMap;
using google::api::expr::runtime::CelValue;

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
  }

  void setNetworkReadFilterCallbacksPtr(Network::ReadFilterCallbacks* cb) {
    network_read_filter_callbacks_ = cb;
  }

  void setNetworkWriteFilterCallbacksPtr(Network::WriteFilterCallbacks* cb) {
    network_write_filter_callbacks_ = cb;
  }
  void setRequestHeaders(Http::RequestHeaderMap* request_headers) {
    request_headers_ = request_headers;
  }

  const LocalInfo::LocalInfo* getRootLocalInfo() const { return root_local_info_; }
  const std::shared_ptr<PluginBase>& getPlugin() const { return plugin_; }
};

class MockCelMap : public CelMap {
public:
  absl::optional<CelValue> operator[](CelValue index) const override {
    return MockIndexOperator(index);
  }
  MOCK_METHOD(absl::optional<CelValue>, MockIndexOperator, (CelValue), (const));
  using CelMap::ListKeys;
  MOCK_METHOD(absl::StatusOr<const CelList*>, ListKeys, (), (const, override));
  MOCK_METHOD(int, size, (), (const, override));
};

class MockCelList : public CelList {
public:
  CelValue operator[](int index) const override { return MockIndexOperator(index); }
  MOCK_METHOD(CelValue, MockIndexOperator, (int), (const));
  MOCK_METHOD(int, size, (), (const, override));
};

class ContextTest : public testing::Test {
protected:
  TestContext ctx_;
  MockCelMap mock_cel_map_;
  MockCelList mock_cel_list_;
};

// Tests the stub functions which are added just for inheritance,
// in order to resolve coverage issue.
// If a function listed in this test has a specific implementation,
// it have to be moved to the other test and have a own test case.
TEST_F(ContextTest, StubFunctionsTest) {
  EXPECT_EQ(ctx_.FindFunctionOverloads("function").size(), 0);
}

// getConstRequestStreamInfo and getRequestStreamInfo should return
// the stream info from encoder_callbacks_, decoder_callbacks_,
// access_log_stream_info_, network_read_filter_callbacks_, or
// network_write_filter_callbacks_ after checking if each one is
// null or not in the given sequence.
TEST_F(ContextTest, GetRequestStreamInfoTest) {
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks;
  Envoy::StreamInfo::MockStreamInfo encoder_si;
  EXPECT_CALL(encoder_callbacks, streamInfo()).WillRepeatedly(testing::ReturnRef(encoder_si));
  ctx_.setEncoderFilterCallbacksPtr(&encoder_callbacks);

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  Envoy::StreamInfo::MockStreamInfo decoder_si;
  EXPECT_CALL(decoder_callbacks, streamInfo())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(decoder_si));
  ctx_.setDecoderFilterCallbacksPtr(&decoder_callbacks);

  Envoy::StreamInfo::MockStreamInfo access_log_si;
  ctx_.setAccessLogStreamInfoPtr(&access_log_si);

  Network::MockReadFilterCallbacks read_filter_callbacks;
  Envoy::Network::MockConnection read_filter_connection;
  Envoy::StreamInfo::MockStreamInfo read_filter_si;
  EXPECT_CALL(read_filter_callbacks, connection())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(read_filter_connection));
  EXPECT_CALL(read_filter_connection, streamInfo())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(read_filter_si));
  ctx_.setNetworkReadFilterCallbacksPtr(&read_filter_callbacks);

  Network::MockWriteFilterCallbacks write_filter_callbacks;
  Envoy::Network::MockConnection write_filter_connection;
  Envoy::StreamInfo::MockStreamInfo write_filter_si;
  EXPECT_CALL(write_filter_callbacks, connection())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(write_filter_connection));
  EXPECT_CALL(write_filter_connection, streamInfo())
      .Times(2)
      .WillRepeatedly(testing::ReturnRef(write_filter_si));
  ctx_.setNetworkWriteFilterCallbacksPtr(&write_filter_callbacks);

  EXPECT_EQ(ctx_.getConstRequestStreamInfo(), &encoder_si);
  EXPECT_EQ(ctx_.getRequestStreamInfo(), &encoder_si);
  ctx_.setEncoderFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx_.getConstRequestStreamInfo(), &decoder_si);
  EXPECT_EQ(ctx_.getRequestStreamInfo(), &decoder_si);
  ctx_.setDecoderFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx_.getConstRequestStreamInfo(), &access_log_si);
  ctx_.setAccessLogStreamInfoPtr(nullptr);

  EXPECT_EQ(ctx_.getConstRequestStreamInfo(), &read_filter_si);
  EXPECT_EQ(ctx_.getRequestStreamInfo(), &read_filter_si);
  ctx_.setNetworkReadFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx_.getConstRequestStreamInfo(), &write_filter_si);
  EXPECT_EQ(ctx_.getRequestStreamInfo(), &write_filter_si);
  ctx_.setNetworkWriteFilterCallbacksPtr(nullptr);
  EXPECT_EQ(ctx_.getConstRequestStreamInfo(), nullptr);
  EXPECT_EQ(ctx_.getRequestStreamInfo(), nullptr);
}

TEST_F(ContextTest, GetConnectionTest) {
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks;
  Envoy::Network::MockConnection encoder_connection;
  EXPECT_CALL(encoder_callbacks, connection())
      .WillRepeatedly(testing::Return(
          makeOptRef(dynamic_cast<const Network::Connection&>(encoder_connection))));
  ctx_.setEncoderFilterCallbacksPtr(&encoder_callbacks);

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  Envoy::Network::MockConnection decoder_connection;
  EXPECT_CALL(decoder_callbacks, connection())
      .WillRepeatedly(testing::Return(
          makeOptRef(dynamic_cast<const Network::Connection&>(decoder_connection))));
  ctx_.setDecoderFilterCallbacksPtr(&decoder_callbacks);

  Envoy::StreamInfo::MockStreamInfo access_log_si;
  ctx_.setAccessLogStreamInfoPtr(&access_log_si);

  Network::MockReadFilterCallbacks read_filter_callbacks;
  Envoy::Network::MockConnection read_filter_connection;
  EXPECT_CALL(read_filter_callbacks, connection())
      .WillRepeatedly(testing::ReturnRef(read_filter_connection));
  ctx_.setNetworkReadFilterCallbacksPtr(&read_filter_callbacks);

  Network::MockWriteFilterCallbacks write_filter_callbacks;
  Envoy::Network::MockConnection write_filter_connection;
  EXPECT_CALL(write_filter_callbacks, connection())
      .WillRepeatedly(testing::ReturnRef(write_filter_connection));
  ctx_.setNetworkWriteFilterCallbacksPtr(&write_filter_callbacks);

  EXPECT_EQ(ctx_.getConnection(), &encoder_connection);
  ctx_.setEncoderFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx_.getConnection(), &decoder_connection);
  ctx_.setDecoderFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx_.getConnection(), &read_filter_connection);
  ctx_.setNetworkReadFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx_.getConnection(), &write_filter_connection);
  ctx_.setNetworkWriteFilterCallbacksPtr(nullptr);

  EXPECT_EQ(ctx_.getConnection(), nullptr);
}

TEST_F(ContextTest, SerializeValueMapTest) {
  std::string result;
  CelValue value = CelValue::CreateMap(&mock_cel_map_);
  EXPECT_CALL(mock_cel_map_, ListKeys())
      .WillOnce(testing::Return(absl::UnimplementedError("CelMap::ListKeys is not implemented")));
  EXPECT_EQ(serializeValue(value, &result), WasmResult::SerializationFailure);

  EXPECT_CALL(mock_cel_list_, MockIndexOperator(_))
      .WillOnce(testing::Return(CelValue::CreateNull()));
  EXPECT_CALL(mock_cel_map_, size()).WillRepeatedly(testing::Return(1));
  EXPECT_CALL(mock_cel_map_, ListKeys()).WillOnce(testing::Return(&mock_cel_list_));
  EXPECT_EQ(serializeValue(value, &result), WasmResult::SerializationFailure);

  EXPECT_CALL(mock_cel_list_, MockIndexOperator(_))
      .Times(2)
      .WillRepeatedly(testing::Return(CelValue::CreateStringView("test")));
  EXPECT_CALL(mock_cel_map_, ListKeys()).WillOnce(testing::Return(&mock_cel_list_));
  EXPECT_CALL(mock_cel_map_, MockIndexOperator(_))
      .WillOnce(testing::Return(CelValue::CreateNull()));
  EXPECT_EQ(serializeValue(value, &result), WasmResult::SerializationFailure);

  EXPECT_CALL(mock_cel_list_, MockIndexOperator(_))
      .Times(2)
      .WillRepeatedly(testing::Return(CelValue::CreateStringView("test")));
  EXPECT_CALL(mock_cel_map_, ListKeys()).WillOnce(testing::Return(&mock_cel_list_));
  EXPECT_CALL(mock_cel_map_, MockIndexOperator(_))
      .WillOnce(testing::Return(CelValue::CreateStringView("test")));
  EXPECT_EQ(serializeValue(value, &result), WasmResult::Ok);
}

TEST_F(ContextTest, SerializeValueListTest) {
  std::string result;
  CelValue value = CelValue::CreateList(&mock_cel_list_);

  EXPECT_CALL(mock_cel_list_, MockIndexOperator(_))
      .WillOnce(testing::Return(CelValue::CreateNull()));
  EXPECT_CALL(mock_cel_list_, size()).WillRepeatedly(testing::Return(1));
  EXPECT_EQ(serializeValue(value, &result), WasmResult::SerializationFailure);

  EXPECT_CALL(mock_cel_list_, MockIndexOperator(_))
      .Times(1)
      .WillRepeatedly(testing::Return(CelValue::CreateStringView("test")));
  EXPECT_EQ(serializeValue(value, &result), WasmResult::Ok);
}

TEST_F(ContextTest, FindValueTest) {
  Protobuf::Arena arena;
  ASSERT_EQ(ctx_.getRootLocalInfo(), nullptr);
  ASSERT_EQ(ctx_.getPlugin(), nullptr);

  EXPECT_FALSE(ctx_.FindValue("node", &arena).has_value());
  EXPECT_FALSE(ctx_.FindValue("listener_direction", &arena).has_value());
  EXPECT_FALSE(ctx_.FindValue("listener_metadata", &arena).has_value());
  EXPECT_FALSE(ctx_.FindValue("plugin_name", &arena).has_value());
}

TEST_F(ContextTest, ClearRouteCacheCalledInDownstreamConfiguration) {

  Http::MockDownstreamStreamFilterCallbacks downstream_callbacks;
  EXPECT_CALL(downstream_callbacks, clearRouteCache()).Times(5).WillRepeatedly(testing::Return());

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  EXPECT_CALL(decoder_callbacks, downstreamCallbacks())
      .WillRepeatedly(testing::Return(
          makeOptRef(dynamic_cast<Http::DownstreamStreamFilterCallbacks&>(downstream_callbacks))));
  ctx_.setDecoderFilterCallbacksPtr(&decoder_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/123"}};
  ctx_.setRequestHeaders(&request_headers);

  ctx_.clearRouteCache();
  ctx_.addHeaderMapValue(WasmHeaderMapType::RequestHeaders, "key", "value");
  ctx_.setHeaderMapPairs(WasmHeaderMapType::RequestHeaders, Pairs{{"key2", "value2"}});
  ctx_.replaceHeaderMapValue(WasmHeaderMapType::RequestHeaders, "key", "value2");
  ctx_.removeHeaderMapValue(WasmHeaderMapType::RequestHeaders, "key");
}

TEST_F(ContextTest, ClearRouteCacheDoesNothingInUpstreamConfiguration) {

  Http::MockStreamDecoderFilterCallbacks decoder_callbacks;
  EXPECT_CALL(decoder_callbacks, downstreamCallbacks())
      .WillRepeatedly(testing::Return(absl::nullopt));
  ctx_.setDecoderFilterCallbacksPtr(&decoder_callbacks);

  Http::TestRequestHeaderMapImpl request_headers{{":path", "/123"}};
  ctx_.setRequestHeaders(&request_headers);

  // Calling methods should be safe.
  ctx_.clearRouteCache();
  ctx_.addHeaderMapValue(WasmHeaderMapType::RequestHeaders, "key", "value");
  ctx_.setHeaderMapPairs(WasmHeaderMapType::RequestHeaders, Pairs{{"key2", "value2"}});
  ctx_.replaceHeaderMapValue(WasmHeaderMapType::RequestHeaders, "key", "value2");
  ctx_.removeHeaderMapValue(WasmHeaderMapType::RequestHeaders, "key");
}

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
