#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using testing::_;
using testing::Invoke;

class McpJsonRestBridgeFilterTest : public testing::Test {
public:
  void SetUp() override {
    McpJsonRestBridgeProtoConfig proto_config;
    // Set up a basic config
    config_ = std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);
    filter_ = std::make_unique<McpJsonRestBridgeFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  McpJsonRestBridgeFilterConfigSharedPtr config_;
  std::unique_ptr<McpJsonRestBridgeFilter> filter_;
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks_;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks_;
};

TEST_F(McpJsonRestBridgeFilterTest, InitializeRequestReturnsServerInfoLocalResponse) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};

  // It should return StopIteration because body is needed for POST requests.
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  constexpr absl::string_view kExpectedResponse =
      R"json({"id":0,"jsonrpc":"2.0","result":{"capabilities":{"tools":{"listChanged":false}},"protocolVersion":"2025-11-25","serverInfo":{"name":"mcp-json-rest-bridge","version":"1.0.0"}}})json";

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::OK, testing::Eq(kExpectedResponse), _, _, _))
      .WillOnce(Invoke([](Http::Code, absl::string_view,
                          std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                          absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        if (modify_headers) {
          Http::TestResponseHeaderMapImpl response_headers;
          modify_headers(response_headers);
        }
      }));

  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0", "id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})json");

  // Decoding data triggers parse and handles 'initialize' method, sending local reply.
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true), Http::FilterDataStatus::Continue);
}

TEST_F(McpJsonRestBridgeFilterTest, NotificationsInitializedMethodReturnsAcceptedHttpCode) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};

  // It should return StopIteration because body is needed for POST requests.
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::Accepted, testing::Eq(""), _, _,
                             testing::Eq("mcp_json_rest_bridge_filter_initialize_ack")))
      .WillOnce(Invoke([](Http::Code, absl::string_view,
                          std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                          absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        Http::TestResponseHeaderMapImpl response_headers;
        if (modify_headers) {
          modify_headers(response_headers);
        }
        EXPECT_EQ(response_headers.get(Http::LowerCaseString("content-length"))[0]
                      ->value()
                      .getStringView(),
                  "0");
      }));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","method":"notifications/initialized"})json");

  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true), Http::FilterDataStatus::Continue);
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
