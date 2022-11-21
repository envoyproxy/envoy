#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "test/integration/base_integration_test.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "contrib/generic_proxy/filters/network/source/proxy.h"
#include "contrib/generic_proxy/filters/network/test/fake_codec.h"
#include "contrib/generic_proxy/filters/network/test/mocks/codec.h"
#include "contrib/generic_proxy/filters/network/test/mocks/filter.h"
#include "contrib/generic_proxy/filters/network/test/mocks/route.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

class IntegrationTest : public testing::TestWithParam<Network::Address::IpVersion> {
public:
  struct ConnectionCallbacks : public Network::ConnectionCallbacks {
    ConnectionCallbacks(IntegrationTest& parent) : parent_(parent) {}

    // Network::ConnectionCallbacks
    void onEvent(Network::ConnectionEvent event) override {
      if (event == Network::ConnectionEvent::Connected) {
        connection_connected_ = true;
      }
      parent_.integration_->dispatcher_->exit();
    }
    void onAboveWriteBufferHighWatermark() override {}
    void onBelowWriteBufferLowWatermark() override {}

    bool connection_connected_{};
    IntegrationTest& parent_;
  };
  using ConnectionCallbacksSharedPtr = std::shared_ptr<ConnectionCallbacks>;

  struct TestReadFilter : Network::ReadFilter {
    TestReadFilter(IntegrationTest& parent) : parent_(parent) {}

    // Network::ReadFilter
    Network::FilterStatus onData(Buffer::Instance& data, bool) override {
      parent_.response_decoder_->decode(data);
      return Network::FilterStatus::Continue;
    }
    Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
    void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override {}

    IntegrationTest& parent_;
  };
  using TestReadFilterSharedPtr = std::shared_ptr<TestReadFilter>;

  struct TestRequestEncoderCallback : public RequestEncoderCallback {
    void onEncodingSuccess(Buffer::Instance& buffer, bool expect_response) override {
      buffer_.move(buffer);
      expect_response_ = expect_response;
      complete_ = true;
      request_bytes_ = buffer_.length();
    }
    bool complete_{};
    size_t request_bytes_{};
    Buffer::OwnedImpl buffer_;
    bool expect_response_;
  };
  using TestRequestEncoderCallbackSharedPtr = std::shared_ptr<TestRequestEncoderCallback>;

  struct TestResponseEncoderCallback : public ResponseEncoderCallback {
    void onEncodingSuccess(Buffer::Instance& buffer, bool) override {
      buffer_.move(buffer);
      complete_ = true;
      response_bytes_ = buffer_.length();
    }
    bool complete_{};
    size_t response_bytes_{};
    Buffer::OwnedImpl buffer_;
  };
  using TestResponseEncoderCallbackSharedPtr = std::shared_ptr<TestResponseEncoderCallback>;

  struct TestResponseDecoderCallback : public ResponseDecoderCallback {
    TestResponseDecoderCallback(IntegrationTest& parent) : parent_(parent) {}

    void onDecodingSuccess(ResponsePtr response) override {
      response_ = std::move(response);
      complete_ = true;
      parent_.integration_->dispatcher_->exit();
    }
    void onDecodingFailure() override {}

    bool complete_{};
    ResponsePtr response_;
    IntegrationTest& parent_;
  };
  using TestResponseDecoderCallbackSharedPtr = std::shared_ptr<TestResponseDecoderCallback>;

  void initialize(const std::string& config_yaml, CodecFactoryPtr codec_factory) {
    integration_ =
        std::make_unique<BaseIntegrationTest>(Network::Address::IpVersion::v4, config_yaml);
    integration_->initialize();

    // Create codec for downstream client.
    codec_factory_ = std::move(codec_factory);
    request_encoder_ = codec_factory_->requestEncoder();
    response_decoder_ = codec_factory_->responseDecoder();
    response_encoder_ = codec_factory_->responseEncoder();
    request_encoder_callback_ = std::make_shared<TestRequestEncoderCallback>();
    response_decoder_callback_ = std::make_shared<TestResponseDecoderCallback>(*this);
    response_encoder_callback_ = std::make_shared<TestResponseEncoderCallback>();
    response_decoder_->setDecoderCallback(*response_decoder_callback_);
  }

  std::string defaultConfig() {
    return absl::StrCat(ConfigHelper::baseConfig(false), R"EOF(
    filter_chains:
      filters:
        name: meta
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.v3.GenericProxy
          stat_prefix: config_test
          filters:
          - name: envoy.filters.generic.router
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.router.v3.Router
          codec_config:
            name: fake
            typed_config:
              "@type": type.googleapis.com/xds.type.v3.TypedStruct
              type_url: envoy.generic_proxy.codecs.fake.type
              value: {}
          route_config:
            name: test-routes
            routes:
              matcher_tree:
                input:
                  name: request-service
                  typed_config:
                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.ServiceMatchInput
                exact_match_map:
                  map:
                    service_name_0:
                      matcher:
                        matcher_list:
                          matchers:
                          - predicate:
                              single_predicate:
                                input:
                                  name: request-properties
                                  typed_config:
                                    "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.matcher.v3.PropertyMatchInput
                                    property_name: version
                                value_match:
                                  exact: v1
                            on_match:
                              action:
                                name: route
                                typed_config:
                                  "@type": type.googleapis.com/envoy.extensions.filters.network.generic_proxy.action.v3.RouteAction
                                  cluster: cluster_0
)EOF");
  }

  // Create client connection.
  bool makeClientConnectionForTest() {
    connection_callbacks_ = std::make_shared<ConnectionCallbacks>(*this);
    test_read_filter_ = std::make_shared<TestReadFilter>(*this);

    client_connection_ = integration_->makeClientConnection(integration_->lookupPort("listener_0"));
    client_connection_->addConnectionCallbacks(*connection_callbacks_);
    client_connection_->addReadFilter(test_read_filter_);
    client_connection_->connect();
    integration_->dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);
    return connection_callbacks_->connection_connected_;
  }

  // Send downstream request.
  void sendRequestForTest(Request& request) {
    request_encoder_->encode(request, *request_encoder_callback_);
    RELEASE_ASSERT(request_encoder_callback_->complete_, "Encoding should complete Immediately");
    client_connection_->write(request_encoder_callback_->buffer_, false);
    client_connection_->dispatcher().run(Envoy::Event::Dispatcher::RunType::NonBlock);
  }

  // Waiting upstream connection to be created.
  void waitForUpstreamConnectionForTest() {
    integration_->waitForNextRawUpstreamConnection({0}, upstream_connection_);
  }

  // Waiting for upstream request data.
  void waitForUpstreamRequestForTest(uint64_t num_bytes, std::string* data) {
    auto result = upstream_connection_->waitForData(num_bytes, data);
    RELEASE_ASSERT(result, result.failure_message());
  }

  // Send upstream response.
  void sendResponseForTest(const Response& response) {
    response_encoder_->encode(response, *response_encoder_callback_);
    RELEASE_ASSERT(response_encoder_callback_->complete_, "Encoding should complete Immediately");

    auto result =
        upstream_connection_->write(response_encoder_callback_->buffer_.toString(), false);
    RELEASE_ASSERT(result, result.failure_message());
  }

  // Waiting for downstream response.
  AssertionResult waitDownstreamResponseForTest(std::chrono::milliseconds timeout) {
    bool timer_fired = false;
    if (!response_decoder_callback_->complete_) {
      Envoy::Event::TimerPtr timer(
          integration_->dispatcher_->createTimer([this, &timer_fired]() -> void {
            timer_fired = true;
            integration_->dispatcher_->exit();
          }));
      timer->enableTimer(timeout);
      integration_->dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);
      if (timer_fired) {
        return AssertionFailure() << "Timed out waiting for response";
      }
    }
    if (!response_decoder_callback_->complete_) {
      return AssertionFailure() << "No response or response not complete";
    }
    return AssertionSuccess();
  }

  void cleanup() {
    if (upstream_connection_ != nullptr) {
      AssertionResult result = upstream_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = upstream_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      upstream_connection_.reset();
    }
    if (client_connection_ != nullptr) {
      client_connection_->close(Envoy::Network::ConnectionCloseType::NoFlush);
    }
  }

  // Codec.
  CodecFactoryPtr codec_factory_;
  RequestEncoderPtr request_encoder_;
  ResponseDecoderPtr response_decoder_;
  ResponseEncoderPtr response_encoder_;
  TestRequestEncoderCallbackSharedPtr request_encoder_callback_;
  TestResponseDecoderCallbackSharedPtr response_decoder_callback_;
  TestResponseEncoderCallbackSharedPtr response_encoder_callback_;

  // Integration test server.
  std::unique_ptr<BaseIntegrationTest> integration_;

  // Callbacks for downstream connection.
  ConnectionCallbacksSharedPtr connection_callbacks_;
  TestReadFilterSharedPtr test_read_filter_;

  // Client connection and upstream connection.
  Network::ClientConnectionPtr client_connection_;
  FakeRawConnectionPtr upstream_connection_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, IntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(IntegrationTest, InitializeInstance) {
  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  initialize(defaultConfig(), std::make_unique<FakeStreamCodecFactory>());
}

TEST_P(IntegrationTest, RequestRouteNotFound) {
  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  initialize(defaultConfig(), std::make_unique<FakeStreamCodecFactory>());
  EXPECT_TRUE(makeClientConnectionForTest());

  FakeStreamCodecFactory::FakeRequest request;
  request.host_ = "service_name_0";
  request.method_ = "hello";
  request.path_ = "/path_or_anything";
  request.protocol_ = "fake_fake_fake";
  request.data_ = {{"version", "v2"}};

  sendRequestForTest(request);

  RELEASE_ASSERT(waitDownstreamResponseForTest(std::chrono::milliseconds(200)),
                 "unexpected timeout");

  EXPECT_NE(response_decoder_callback_->response_, nullptr);
  EXPECT_EQ(response_decoder_callback_->response_->status().message(), "route_not_found");

  cleanup();
}

TEST_P(IntegrationTest, RequestAndResponse) {
  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  initialize(defaultConfig(), std::make_unique<FakeStreamCodecFactory>());

  EXPECT_TRUE(makeClientConnectionForTest());

  FakeStreamCodecFactory::FakeRequest request;
  request.host_ = "service_name_0";
  request.method_ = "hello";
  request.path_ = "/path_or_anything";
  request.protocol_ = "fake_fake_fake";
  request.data_ = {{"version", "v1"}};

  sendRequestForTest(request);

  waitForUpstreamConnectionForTest();
  waitForUpstreamRequestForTest(request_encoder_callback_->request_bytes_, nullptr);

  FakeStreamCodecFactory::FakeResponse response;
  response.protocol_ = "fake_fake_fake";
  response.status_ = Status();
  response.data_["zzzz"] = "xxxx";

  sendResponseForTest(response);

  RELEASE_ASSERT(waitDownstreamResponseForTest(std::chrono::milliseconds(200)),
                 "unexpected timeout");

  EXPECT_NE(response_decoder_callback_->response_, nullptr);
  EXPECT_EQ(response_decoder_callback_->response_->status().code(), StatusCode::kOk);
  EXPECT_EQ(response_decoder_callback_->response_->getByKey("zzzz"), "xxxx");

  cleanup();
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
