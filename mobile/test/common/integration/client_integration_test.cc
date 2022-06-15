#include "source/extensions/http/header_formatters/preserve_case/config.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "test/common/integration/base_client_integration_test.h"
#include "test/integration/autonomous_upstream.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/data/utility.h"
#include "library/common/engine.h"
#include "library/common/http/header_utility.h"
#include "library/common/types/c_types.h"

using testing::ReturnRef;

namespace Envoy {
namespace {

class ClientIntegrationTest : public BaseClientIntegrationTest,
                              public testing::TestWithParam<Network::Address::IpVersion> {
public:
  ClientIntegrationTest() : BaseClientIntegrationTest(/*ip_version=*/GetParam()) {}

  void SetUp() override {
    setUpstreamCount(config_helper_.bootstrap().static_resources().clusters_size());
    // TODO(abeyad): Add paramaterized tests for HTTP1, HTTP2, and HTTP3.
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  void TearDown() override {
    ASSERT_EQ(cc_.on_complete_calls + cc_.on_cancel_calls + cc_.on_error_calls, 1);
    cleanup();
    BaseClientIntegrationTest::TearDown();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClientIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ClientIntegrationTest, Basic) {
  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  custom_headers_.emplace(AutonomousStream::EXPECT_REQUEST_SIZE_BYTES,
                          std::to_string(request_data.length()));
  initialize();

  stream_prototype_->setOnData([this](envoy_data c_data, bool end_stream) {
    if (end_stream) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "");
    } else {
      EXPECT_EQ(c_data.length, 10);
    }
    cc_.on_data_calls++;
    release_envoy_data(c_data);
  });

  stream_->sendHeaders(default_request_headers_, false);

  envoy_data c_data = Data::Utility::toBridgeData(request_data);
  stream_->sendData(c_data);

  Platform::RequestTrailersBuilder builder;
  std::shared_ptr<Platform::RequestTrailers> trailers =
      std::make_shared<Platform::RequestTrailers>(builder.build());
  stream_->close(trailers);

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 2);
  ASSERT_EQ(cc_.on_complete_calls, 1);
  ASSERT_EQ(cc_.on_header_consumed_bytes_from_response, 27);
  ASSERT_EQ(cc_.on_complete_received_byte_count, 67);
}

TEST_P(ClientIntegrationTest, BasicNon2xx) {
  initialize();

  // Set response header status to be non-2xx to test that the correct stats get charged.
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl({{":status", "503"}, {"content-length", "0"}})));

  stream_->sendHeaders(default_request_headers_, true);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls, 0);
  ASSERT_EQ(cc_.status, "503");
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_P(ClientIntegrationTest, BasicReset) {
  custom_headers_.emplace(AutonomousStream::RESET_AFTER_REQUEST, "yes");
  initialize();

  stream_->sendHeaders(default_request_headers_, true);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls, 1);
  ASSERT_EQ(cc_.on_headers_calls, 0);
}

TEST_P(ClientIntegrationTest, BasicCancel) {
  autonomous_upstream_ = false;
  initialize();
  ConditionalInitializer headers_callback;

  stream_prototype_->setOnHeaders(
      [this, &headers_callback](Platform::ResponseHeadersSharedPtr headers, bool,
                                envoy_stream_intel) {
        cc_.status = absl::StrCat(headers->httpStatus());
        cc_.on_headers_calls++;
        headers_callback.setReady();
        return nullptr;
      });

  stream_->sendHeaders(default_request_headers_, true);

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  // Send an incomplete response.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 15\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  headers_callback.waitReady();
  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);

  // Now cancel, and make sure the cancel is received.
  stream_->cancel();
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  ASSERT_EQ(cc_.on_cancel_calls, 1);
}

TEST_P(ClientIntegrationTest, CancelWithPartialStream) {
  autonomous_upstream_ = false;
  explicit_flow_control_ = true;
  initialize();
  ConditionalInitializer headers_callback;

  stream_prototype_->setOnHeaders(
      [this, &headers_callback](Platform::ResponseHeadersSharedPtr headers, bool,
                                envoy_stream_intel) {
        cc_.status = absl::StrCat(headers->httpStatus());
        cc_.on_headers_calls++;
        headers_callback.setReady();
        return nullptr;
      });

  stream_->sendHeaders(default_request_headers_, true);

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  // Send a complete response with body.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nasd";
  ASSERT_TRUE(upstream_connection->write(response));
  headers_callback.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  // Due to explicit flow control, the upstream stream is complete, but the
  // callbacks will not be called for data and completion. Cancel the stream
  // and make sure the cancel is received.
  stream_->cancel();
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  ASSERT_EQ(cc_.on_cancel_calls, 1);
}

// TODO(junr03): test with envoy local reply with local stream not closed, which causes a reset
// fired from the Http:ConnectionManager rather than the Http::Client. This cannot be done in
// unit tests because the Http::ConnectionManager is mocked using a mock response encoder.

// Test header key case sensitivity.
TEST_P(ClientIntegrationTest, CaseSensitive) {
  custom_headers_.emplace("FoO", "bar");
  autonomous_upstream_ = false;
  initialize();

  stream_prototype_->setOnHeaders(
      [this](Platform::ResponseHeadersSharedPtr headers, bool, envoy_stream_intel) {
        cc_.status = absl::StrCat(headers->httpStatus());
        cc_.on_headers_calls++;
        EXPECT_TRUE(headers->contains("My-ResponsE-Header"));
        EXPECT_TRUE((*headers)["My-ResponsE-Header"][0] == "foo");
        return nullptr;
      });

  stream_->sendHeaders(default_request_headers_, true);

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  // Verify that the upstream request has preserved cased headers.
  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));
  EXPECT_TRUE(absl::StrContains(upstream_request, "FoO: bar")) << upstream_request;

  // Send mixed case headers, and verify via setOnHeaders they are received correctly.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nMy-ResponsE-Header: foo\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 1);
}

TEST_P(ClientIntegrationTest, TimeoutOnRequestPath) {
  setStreamIdleTimeoutSeconds(1);

  autonomous_upstream_ = false;
  initialize();

  stream_->sendHeaders(default_request_headers_, false);

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 0);
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  ASSERT_EQ(cc_.on_error_calls, 1);
}

TEST_P(ClientIntegrationTest, TimeoutOnResponsePath) {
  setStreamIdleTimeoutSeconds(1);
  autonomous_upstream_ = false;
  initialize();

  stream_->sendHeaders(default_request_headers_, true);

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));

  // Send response headers but no body.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\nMy-ResponsE-Header: foo\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 0);
  ASSERT_EQ(cc_.on_complete_calls, 0);
  ASSERT_EQ(cc_.on_error_calls, 1);
}

} // namespace
} // namespace Envoy
