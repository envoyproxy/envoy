#include "envoy/extensions/filters/http/a2a/v3/a2a.pb.h"

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
  A2aFilterTest() {
    envoy::extensions::filters::http::a2a::v3::A2a proto_config;
    proto_config.set_traffic_mode(envoy::extensions::filters::http::a2a::v3::A2a::PASS_THROUGH);

    config_ =
        std::make_shared<A2aFilterConfig>(proto_config, "test_prefix", *stats_store_.rootScope());
    filter_ = std::make_unique<A2aFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  Stats::IsolatedStoreImpl stats_store_;
  A2aFilterConfigSharedPtr config_;
  std::unique_ptr<A2aFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};

TEST_F(A2aFilterTest, ValidGetRequest) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

TEST_F(A2aFilterTest, ValidDeleteRequest) {
  Http::TestRequestHeaderMapImpl headers{{":method", "DELETE"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

TEST_F(A2aFilterTest, ValidPostRequest) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {"content-type", "application/json"}};
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
  config_ =
      std::make_shared<A2aFilterConfig>(proto_config, "test_prefix", *stats_store_.rootScope());
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

TEST_F(A2aFilterTest, TrafficModeReject) {
  // Set global config to REJECT
  envoy::extensions::filters::http::a2a::v3::A2a proto_config;
  proto_config.set_traffic_mode(envoy::extensions::filters::http::a2a::v3::A2a::REJECT);
  config_ = std::make_shared<A2aFilterConfig>(proto_config, "test_prefix", *stats_store_.rootScope());
  filter_ = std::make_unique<A2aFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "text/plain"}};

  // Ensure no override config is returned
  EXPECT_CALL(*decoder_callbacks_.route_, mostSpecificPerFilterConfig(_))
      .WillRepeatedly(Return(nullptr));

  // Should reject because global config is REJECT and request is not A2A
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

} // namespace
} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
