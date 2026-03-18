#include "envoy/extensions/filters/http/a2a/v3/a2a.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/a2a/a2a_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {
namespace {

class A2aFilterTest : public testing::Test {
public:
  A2aFilterTest() : stats_(A2aFilterStats{A2A_FILTER_STATS(POOL_COUNTER(scope_))}) {
    envoy::extensions::filters::http::a2a::v3::A2a proto_config;
    proto_config.set_traffic_mode(envoy::extensions::filters::http::a2a::v3::A2a::PASS_THROUGH);
    config_ = std::make_shared<A2aFilterConfig>(proto_config, "test_prefix", *scope_.rootScope());
    filter_ = std::make_unique<A2aFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  Stats::IsolatedStoreImpl scope_;
  A2aFilterStats stats_;
  A2aFilterConfigSharedPtr config_;
  std::unique_ptr<A2aFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};

TEST_F(A2aFilterTest, ValidGetRequest) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

TEST_F(A2aFilterTest, ValidPostRequest) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

TEST_F(A2aFilterTest, ValidPostRequestWithA2aContentType) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/a2a+json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

TEST_F(A2aFilterTest, InvalidPostRequestJsonPrefixMismatch) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/jsonp"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

TEST_F(A2aFilterTest, InvalidPostRequestA2aJsonPrefixMismatch) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/a2a+jsonp"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

TEST_F(A2aFilterTest, ValidPostRequestWithJsonAndCharset) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json; charset=utf-8"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

TEST_F(A2aFilterTest, ValidPostRequestWithJsonAndWhitespace) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json "}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

TEST_F(A2aFilterTest, ValidPostRequestWithA2aJsonAndCharset) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/a2a+json; charset=utf-8"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

TEST_F(A2aFilterTest, InvalidPostRequestNoJson) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "text/plain"}};

  // PASS_THROUGH mode
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

TEST_F(A2aFilterTest, InvalidPostRequestRejectMode) {
  envoy::extensions::filters::http::a2a::v3::A2a proto_config;
  proto_config.set_traffic_mode(envoy::extensions::filters::http::a2a::v3::A2a::REJECT);
  config_ = std::make_shared<A2aFilterConfig>(proto_config, "test_prefix", *scope_.rootScope());
  filter_ = std::make_unique<A2aFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "text/plain"}};

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

TEST_F(A2aFilterTest, DecodeDataValidJson) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "message/send",
    "id": "123",
    "params": {
      "taskId": "task-abc"
    }
  })";
  Buffer::OwnedImpl buffer(json);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

TEST_F(A2aFilterTest, DecodeDataInvalidJson) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  const std::string json = R"({ invalid json })";
  Buffer::OwnedImpl buffer(json);

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _, _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, true));
}

TEST_F(A2aFilterTest, DecodeDataPartialJson) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  const std::string part1 = R"({ "jsonrpc": "2.0", )";
  Buffer::OwnedImpl buffer1(part1);

  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer1, false));

  const std::string part2 = R"("method": "message/send", "id": "1", "params": {}})";
  Buffer::OwnedImpl buffer2(part2);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer2, true));
}

TEST_F(A2aFilterTest, DecodeDataBodyTooLarge) {
  envoy::extensions::filters::http::a2a::v3::A2a proto_config;
  proto_config.set_traffic_mode(envoy::extensions::filters::http::a2a::v3::A2a::PASS_THROUGH);
  proto_config.mutable_max_request_body_size()->set_value(10); // Very small limit
  config_ = std::make_shared<A2aFilterConfig>(proto_config, "test_prefix", *scope_.rootScope());
  filter_ = std::make_unique<A2aFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  const std::string json = R"({ "jsonrpc": "2.0", "method": "long_method_name_exceeding_limit" })";
  Buffer::OwnedImpl buffer(json);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest, "request body is too large.", _, _, _));
  // Should increment body_too_large stat

  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, true));
  EXPECT_EQ(1, config_->stats().body_too_large_.value());
}

TEST_F(A2aFilterTest, DecodeDataBodyTooLargePartial) {
  envoy::extensions::filters::http::a2a::v3::A2a proto_config;
  proto_config.set_traffic_mode(envoy::extensions::filters::http::a2a::v3::A2a::PASS_THROUGH);
  proto_config.mutable_max_request_body_size()->set_value(20);
  config_ = std::make_shared<A2aFilterConfig>(proto_config, "test_prefix", *scope_.rootScope());
  filter_ = std::make_unique<A2aFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  const std::string part1 = R"({ "jsonrpc": "2.0")"; // 17 bytes
  Buffer::OwnedImpl buffer1(part1);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer1, false));

  const std::string part2 = R"(, "method": "foo" })"; // Exceeds limit total
  Buffer::OwnedImpl buffer2(part2);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest, "request body is too large.", _, _, _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer2, true));
  EXPECT_EQ(1, config_->stats().body_too_large_.value());
}

TEST_F(A2aFilterTest, DecodeDataFragmentedBuffer) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl buffer;
  std::string fragment1 = R"({ "jsonrpc")";
  std::string fragment2 = R"(: "2.0", "method")";
  std::string fragment3 = R"(: "message/send", "id": "123" })";

  // Use appendSliceForTest to simulate raw slices.
  buffer.appendSliceForTest(fragment1);
  buffer.appendSliceForTest(fragment2);
  buffer.appendSliceForTest(fragment3);

  // Verify multiple slices are present
  EXPECT_GT(buffer.getRawSlices().size(), 1);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

TEST_F(A2aFilterTest, DecodeDataResumesParsing) {
  // This test proves that bytes_parsed_ correctly tracks progress.
  // If bytes_parsed_ was not working, the second call to decodeData would
  // try to parse the beginning of the buffer again, which would likely fail
  // or result in incorrect state.

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // Chunk 1
  const std::string part1 = R"({ "jsonrpc": "2.0", )";
  Buffer::OwnedImpl buffer1(part1);

  // Expect StopIterationAndWatermark to buffer data
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndWatermark, filter_->decodeData(buffer1, false));

  // Chunk 2 - separate buffer
  const std::string part2 = R"("method": "message/send", "id": "1", "params": {}})";
  Buffer::OwnedImpl buffer2(part2);

  // If bytes_parsed_ works, it resumes parsing from where part1 ended.
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer2, true));
}

TEST_F(A2aFilterTest, DecodeDataIncompleteAtEndStream) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  const std::string json = R"({ "jsonrpc": "2.0" )"; // incomplete
  Buffer::OwnedImpl buffer(json);

  // When end_stream is true but JSON is incomplete/invalid
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _, _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, true));
}

TEST_F(A2aFilterTest, NonJsonOrA2aRequestPassthrough) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "text/plain"}};
  // is_json_post_request_ = false
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));

  Buffer::OwnedImpl buffer("some data");
  // Should return Continue immediately
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

TEST_F(A2aFilterTest, ParsingAlreadyComplete) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // Use a method with scalar params to ensure early stop works reliably
  const std::string json =
      R"({ "jsonrpc": "2.0", "method": "tasks/pushNotificationConfig/delete", "id": "1", "params": {"id": "t1", "pushNotificationConfigId": "c1"} })";
  Buffer::OwnedImpl buffer(json);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false)); // Complete

  // Subsequent call with more data (e.g. trailing characters or just another chunk)
  Buffer::OwnedImpl buffer2(" more data");
  // Should return Continue immediately as parsing_complete_ is true
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer2, true));
}

TEST_F(A2aFilterTest, MaxBodySizeZeroMeansUnlimited) {
  envoy::extensions::filters::http::a2a::v3::A2a proto_config;
  proto_config.mutable_max_request_body_size()->set_value(0); // Unlimited
  config_ = std::make_shared<A2aFilterConfig>(proto_config, "test_prefix", *scope_.rootScope());
  filter_ = std::make_unique<A2aFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // Large payload
  std::string large_json = R"({ "jsonrpc": "2.0", "method": "message/send", "params": { "data": ")";
  large_json.append(10000, 'a'); // 10KB data
  large_json.append(R"(" } })");

  Buffer::OwnedImpl buffer(large_json);

  // Should succeed despite being large
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

TEST_F(A2aFilterTest, RejectInvalidA2aRequest) {
  // Configured to REJECT
  envoy::extensions::filters::http::a2a::v3::A2a proto_config;
  proto_config.set_traffic_mode(envoy::extensions::filters::http::a2a::v3::A2a::REJECT);
  config_ = std::make_shared<A2aFilterConfig>(proto_config, "test_prefix", *scope_.rootScope());
  filter_ = std::make_unique<A2aFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  // Valid JSON but invalid A2A (missing jsonrpc version for example)
  const std::string json = R"({ "method": "message/send" })";
  Buffer::OwnedImpl buffer(json);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "request must be a valid JSON-RPC 2.0 message for A2A", _, _, _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, true));
}

TEST_F(A2aFilterTest, DecodeDataSetsDynamicMetadata) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));

  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "message/send",
    "id": "123",
    "params": {
      "taskId": "task-abc"
    }
  })";
  Buffer::OwnedImpl buffer(json);

  // Expectations
  EXPECT_CALL(decoder_callbacks_, filterConfigName()).WillOnce(Return("a2a_filter"));
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("a2a_filter", _));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

} // namespace
} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
