#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "test/cc/integration/base/test_engine_builder.h"
#include "test/cc/integration/base/test_engine_and_server.h"
#include "test/test_common/utility.h"
#include "library/cc/stream_prototype.h"
#include "gtest/gtest.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "library/cc/stream.h"
#include "library/common/engine_types.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"
#include "test/common/http/filters/assertion/filter.pb.h"
#include "test/common/integration/test_server.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base_logger.h"

namespace Envoy {
namespace {

class BaseEngineBuilderTest : public testing::TestWithParam<TestServerType> {
protected:
  BaseEngineBuilderTest() : server_type_(GetParam()) {}

  void
  SetUpEngineAndServer(absl::optional<envoymobile::extensions::filters::http::assertion::Assertion>
                           assertion_config = std::nullopt,
                       const absl::flat_hash_map<std::string, std::string>& headers = {},
                       absl::string_view body = "",
                       const absl::flat_hash_map<std::string, std::string>& trailers = {}) {
    absl::Notification engine_running;
    Platform::TestEngineBuilder engine_builder;

    if (assertion_config.has_value()) {
      ::envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter
          assertion_filter;
      assertion_filter.set_name("envoy.filters.http.assertion");
      assertion_filter.mutable_typed_config()->PackFrom(assertion_config.value());
      engine_builder.addHcmHttpFilter(std::move(assertion_filter));
    }

    engine_builder.enableLogger(false).setLogLevel(Logger::Logger::debug).setOnEngineRunning([&]() {
      engine_running.Notify();
    });
    client_engine_with_test_server_ = std::make_unique<TestEngineAndServer>(
        engine_builder, server_type_, headers, body, trailers);
    engine_running.WaitForNotification();
  }

  std::shared_ptr<Platform::Stream> StartStream(Platform::StreamPrototypeSharedPtr stream_prototype,
                                                bool end_stream = true) {
    on_data_was_called_ = false;
    on_headers_was_called_ = false;
    on_trailers_was_called_ = false;
    actual_status_code_ = "";
    actual_response_body_ = "";
    actual_trailer_value_ = "";
    actual_end_stream_ = false;

    EnvoyStreamCallbacks callbacks;
    callbacks.on_headers_ = [this](const Http::ResponseHeaderMap& headers, bool end_stream,
                                   envoy_stream_intel) {
      on_headers_was_called_ = true;
      actual_end_stream_ = end_stream;
      auto status = headers.get(Http::LowerCaseString(":status"));
      if (!status.empty()) {
        actual_status_code_ = std::string(status[0]->value().getStringView());
      }
    };
    callbacks.on_data_ = [this](const Buffer::Instance& data, uint64_t, bool end_stream,
                                envoy_stream_intel) {
      on_data_was_called_ = true;
      actual_end_stream_ = end_stream;
      actual_response_body_ += data.toString();
    };
    callbacks.on_trailers_ = [this](const Http::ResponseTrailerMap& trailers, envoy_stream_intel) {
      on_trailers_was_called_ = true;
      actual_end_stream_ = true;
      auto trailer = trailers.get(Http::LowerCaseString("test-trailer"));
      if (!trailer.empty()) {
        actual_trailer_value_ = std::string(trailer[0]->value().getStringView());
      }
    };
    callbacks.on_complete_ = [this](envoy_stream_intel, envoy_final_stream_intel) {
      stream_completed_.Notify();
    };

    auto stream = stream_prototype->start(std::move(callbacks));

    auto headers = std::make_unique<Http::TestRequestHeaderMapImpl>();
    headers->addCopy(Http::LowerCaseString(":method"), "GET");
    headers->addCopy(Http::LowerCaseString(":scheme"), "http");
    headers->addCopy(Http::LowerCaseString(":authority"),
                     client_engine_with_test_server_->test_server().getAddress());
    headers->addCopy(Http::LowerCaseString(":path"), "/");
    stream->sendHeaders(std::move(headers), end_stream);
    return stream;
  }

  TestServerType server_type_;
  std::unique_ptr<TestEngineAndServer> client_engine_with_test_server_;
  absl::Notification stream_completed_;
  bool on_data_was_called_ = false;
  bool on_headers_was_called_ = false;
  bool on_trailers_was_called_ = false;
  bool actual_end_stream_ = false;
  std::string actual_status_code_;
  std::string actual_trailer_value_;
  std::string actual_response_body_;
};

INSTANTIATE_TEST_SUITE_P(BaseEngineBuilderTest, BaseEngineBuilderTest,
                         testing::Values(TestServerType::HTTP1_WITHOUT_TLS,
                                         TestServerType::HTTP1_WITH_TLS,
                                         TestServerType::HTTP2_WITH_TLS, TestServerType::HTTP3));

TEST_P(BaseEngineBuilderTest, GetRequest) {
  SetUpEngineAndServer(/*assertion_config=*/std::nullopt,
                       /*headers=*/{{"test-header", "test-value"}},
                       /*body=*/"test body");
  auto engine = client_engine_with_test_server_->engine();
  auto stream_prototype = engine->streamClient()->newStreamPrototype();
  StartStream(stream_prototype);
  stream_completed_.WaitForNotification();

  EXPECT_TRUE(on_headers_was_called_);
  EXPECT_TRUE(on_data_was_called_);
  EXPECT_TRUE(actual_end_stream_);
  EXPECT_EQ(actual_status_code_, "200");
  EXPECT_EQ(actual_response_body_, "test body");
}

TEST_P(BaseEngineBuilderTest, GetRequestWithTrailers) {
  SetUpEngineAndServer(/*assertion_config=*/std::nullopt,
                       /*headers=*/{{"test-header", "test-value"}},
                       /*body=*/"test body",
                       /*trailers=*/{{"test-trailer", "test-trailer-value"}});
  auto engine = client_engine_with_test_server_->engine();
  auto stream_prototype = engine->streamClient()->newStreamPrototype();
  StartStream(stream_prototype);
  stream_completed_.WaitForNotification();

  EXPECT_TRUE(on_headers_was_called_);
  EXPECT_TRUE(on_data_was_called_);
  EXPECT_TRUE(on_trailers_was_called_);
  EXPECT_TRUE(actual_end_stream_);
  EXPECT_EQ(actual_status_code_, "200");
  EXPECT_EQ(actual_response_body_, "test body");
  EXPECT_EQ(actual_trailer_value_, "test-trailer-value");
}

TEST_P(BaseEngineBuilderTest, PostRequestWithBody) {
  SetUpEngineAndServer();
  auto engine = client_engine_with_test_server_->engine();
  auto stream_prototype = engine->streamClient()->newStreamPrototype();
  auto stream = StartStream(stream_prototype, /*end_stream=*/false);

  auto buffer = std::make_unique<Buffer::OwnedImpl>("post body");
  stream->close(std::move(buffer));
  stream_completed_.WaitForNotification();

  EXPECT_TRUE(on_headers_was_called_);
  EXPECT_TRUE(on_data_was_called_);
  EXPECT_TRUE(actual_end_stream_);
  EXPECT_EQ(actual_status_code_, "200");
}

} // namespace
} // namespace Envoy
