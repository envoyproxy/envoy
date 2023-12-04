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

class GenericProxyIntegrationTest : public BaseIntegrationTest {
public:
  GenericProxyIntegrationTest(const std::string config_yaml)
      : BaseIntegrationTest(Network::Address::IpVersion::v4, config_yaml) {
    skip_tag_extraction_rule_check_ = true;
  };
};

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
    Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override {
      parent_.client_codec_->decode(data, end_stream);
      return Network::FilterStatus::Continue;
    }
    Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
    void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override {}

    IntegrationTest& parent_;
  };
  using TestReadFilterSharedPtr = std::shared_ptr<TestReadFilter>;

  struct TestRequestEncoderCallback : public EncodingCallbacks {
    void onEncodingSuccess(Buffer::Instance& buffer, bool) override { buffer_.move(buffer); }
    Buffer::OwnedImpl buffer_;
  };
  using TestRequestEncoderCallbackSharedPtr = std::shared_ptr<TestRequestEncoderCallback>;

  struct TestResponseEncoderCallback : public EncodingCallbacks {
    void onEncodingSuccess(Buffer::Instance& buffer, bool) override { buffer_.move(buffer); }
    Buffer::OwnedImpl buffer_;
  };
  using TestResponseEncoderCallbackSharedPtr = std::shared_ptr<TestResponseEncoderCallback>;

  struct TestResponseDecoderCallback : public ClientCodecCallbacks {
    TestResponseDecoderCallback(IntegrationTest& parent) : parent_(parent) {}

    struct SingleResponse {
      bool end_stream_{};
      ResponsePtr response_;
      std::list<StreamFramePtr> response_frames_;
    };

    void onDecodingSuccess(StreamFramePtr response_frame) override {
      auto& response = responses_[response_frame->frameFlags().streamFlags().streamId()];

      ASSERT(!response.end_stream_);
      response.end_stream_ = response_frame->frameFlags().endStream();

      if (response.response_ != nullptr) {
        response.response_frames_.push_back(std::move(response_frame));
      } else {
        ASSERT(response.response_frames_.empty());
        StreamFramePtrHelper<Response> helper(std::move(response_frame));
        ASSERT(helper.typed_frame_ != nullptr);
        response.response_ = std::move(helper.typed_frame_);
      }

      // Exit dispatcher if we have received all the expected response frames.
      if (responses_[waiting_for_stream_id_].end_stream_) {
        parent_.integration_->dispatcher_->exit();
      }
    }
    void onDecodingFailure() override {}
    void writeToConnection(Buffer::Instance&) override {}
    OptRef<Network::Connection> connection() override {
      if (parent_.upstream_connection_ != nullptr) {
        return parent_.upstream_connection_->connection();
      }
      return {};
    }

    uint64_t waiting_for_stream_id_{};
    std::map<uint64_t, SingleResponse> responses_;
    IntegrationTest& parent_;
  };
  using TestResponseDecoderCallbackSharedPtr = std::shared_ptr<TestResponseDecoderCallback>;

  void initialize(const std::string& config_yaml, CodecFactoryPtr codec_factory) {
    integration_ = std::make_unique<GenericProxyIntegrationTest>(config_yaml);
    integration_->initialize();

    // Create codec for downstream client to encode request and decode response.
    codec_factory_ = std::move(codec_factory);
    client_codec_ = codec_factory_->createClientCodec();

    request_encoder_callback_ = std::make_shared<TestRequestEncoderCallback>();
    response_decoder_callback_ = std::make_shared<TestResponseDecoderCallback>(*this);
    client_codec_->setCodecCallbacks(*response_decoder_callback_);

    // Helper codec for upstream server to encode response.
    server_codec_ = codec_factory_->createServerCodec();
    response_encoder_callback_ = std::make_shared<TestResponseEncoderCallback>();
  }

  std::string defaultConfig(bool bind_upstream_connection = false) {
    return absl::StrCat(ConfigHelper::baseConfig(false), fmt::format(R"EOF(
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
              bind_upstream_connection: {}
          codec_config:
            name: fake
            typed_config:
              "@type": type.googleapis.com/xds.type.v3.TypedStruct
              type_url: envoy.generic_proxy.codecs.fake.type
              value: {{}}
          route_config:
            name: test-routes
            virtual_hosts:
            - name: test
              hosts:
              - "*"
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
)EOF",
                                                                     bind_upstream_connection));
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
  void sendRequestForTest(StreamFrame& request) {
    client_codec_->encode(request, *request_encoder_callback_);
    client_connection_->write(request_encoder_callback_->buffer_, false);
    client_connection_->dispatcher().run(Envoy::Event::Dispatcher::RunType::NonBlock);
    // Clear buffer for next encoding.
    request_encoder_callback_->buffer_.drain(request_encoder_callback_->buffer_.length());
  }

  // Waiting upstream connection to be created.
  void waitForUpstreamConnectionForTest() {
    integration_->waitForNextRawUpstreamConnection({0}, upstream_connection_);
  }

  // Waiting for upstream request data.
  void
  waitForUpstreamRequestForTest(const std::function<bool(const std::string&)>& data_validator) {
    auto result = upstream_connection_->waitForData(data_validator, nullptr);
    RELEASE_ASSERT(result, result.failure_message());
    // Clear data for next test.
    upstream_connection_->clearData();
  }

  // Send upstream response.
  void sendResponseForTest(const StreamFrame& response) {
    server_codec_->encode(response, *response_encoder_callback_);

    auto result =
        upstream_connection_->write(response_encoder_callback_->buffer_.toString(), false);
    // Clear buffer for next encoding.
    response_encoder_callback_->buffer_.drain(response_encoder_callback_->buffer_.length());
    RELEASE_ASSERT(result, result.failure_message());
  }

  // Waiting for downstream response.
  AssertionResult waitDownstreamResponseForTest(std::chrono::milliseconds timeout,
                                                uint64_t stream_id) {
    bool timer_fired = false;
    if (!response_decoder_callback_->responses_[stream_id].end_stream_) {
      Envoy::Event::TimerPtr timer(
          integration_->dispatcher_->createTimer([this, &timer_fired]() -> void {
            timer_fired = true;
            integration_->dispatcher_->exit();
          }));
      timer->enableTimer(timeout);
      response_decoder_callback_->waiting_for_stream_id_ = stream_id;
      integration_->dispatcher_->run(Envoy::Event::Dispatcher::RunType::Block);
      if (timer_fired) {
        return AssertionFailure() << "Timed out waiting for response";
      }
      if (timer->enabled()) {
        timer->disableTimer();
      }
    }
    if (!response_decoder_callback_->responses_[stream_id].end_stream_) {
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
  ServerCodecPtr server_codec_;
  ClientCodecPtr client_codec_;

  TestRequestEncoderCallbackSharedPtr request_encoder_callback_;
  TestResponseDecoderCallbackSharedPtr response_decoder_callback_;
  TestResponseEncoderCallbackSharedPtr response_encoder_callback_;

  // Integration test server.
  std::unique_ptr<GenericProxyIntegrationTest> integration_;

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

  RELEASE_ASSERT(waitDownstreamResponseForTest(TestUtility::DefaultTimeout, 0),
                 "unexpected timeout");

  EXPECT_NE(response_decoder_callback_->responses_[0].response_, nullptr);
  EXPECT_EQ(response_decoder_callback_->responses_[0].response_->status().message(),
            "route_not_found");

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
  const std::function<bool(const std::string&)> data_validator =
      [](const std::string& data) -> bool { return data.find("v1") != std::string::npos; };
  waitForUpstreamRequestForTest(data_validator);

  FakeStreamCodecFactory::FakeResponse response;
  response.protocol_ = "fake_fake_fake";
  response.status_ = Status();
  response.data_["zzzz"] = "xxxx";

  sendResponseForTest(response);

  RELEASE_ASSERT(waitDownstreamResponseForTest(TestUtility::DefaultTimeout, 0),
                 "unexpected timeout");

  EXPECT_NE(response_decoder_callback_->responses_[0].response_, nullptr);
  EXPECT_EQ(response_decoder_callback_->responses_[0].response_->status().code(), StatusCode::kOk);
  EXPECT_EQ(response_decoder_callback_->responses_[0].response_->get("zzzz"), "xxxx");

  cleanup();
}

TEST_P(IntegrationTest, MultipleRequestsWithSameStreamId) {
  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  auto codec_factory = std::make_unique<FakeStreamCodecFactory>();

  initialize(defaultConfig(true), std::move(codec_factory));

  EXPECT_TRUE(makeClientConnectionForTest());

  FakeStreamCodecFactory::FakeRequest request_1;
  request_1.host_ = "service_name_0";
  request_1.method_ = "hello";
  request_1.path_ = "/path_or_anything";
  request_1.protocol_ = "fake_fake_fake";
  request_1.data_ = {{"version", "v1"}, {"stream_id", "1"}};

  sendRequestForTest(request_1);

  waitForUpstreamConnectionForTest();
  const std::function<bool(const std::string&)> data_validator =
      [](const std::string& data) -> bool { return data.find("v1") != std::string::npos; };
  waitForUpstreamRequestForTest(data_validator);

  FakeStreamCodecFactory::FakeRequest request_2;
  request_2.host_ = "service_name_0";
  request_2.method_ = "hello";
  request_2.path_ = "/path_or_anything";
  request_2.protocol_ = "fake_fake_fake";
  request_2.data_ = {{"version", "v1"}, {"stream_id", "1"}};

  // Send the second request with the same stream id and expect the connection to be closed.
  sendRequestForTest(request_2);

  // Wait for the connection to be closed.
  auto result = upstream_connection_->waitForDisconnect();
  RELEASE_ASSERT(result, result.message());

  cleanup();
}

TEST_P(IntegrationTest, MultipleRequests) {
  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  auto codec_factory = std::make_unique<FakeStreamCodecFactory>();

  initialize(defaultConfig(true), std::move(codec_factory));

  EXPECT_TRUE(makeClientConnectionForTest());

  FakeStreamCodecFactory::FakeRequest request_1;
  request_1.host_ = "service_name_0";
  request_1.method_ = "hello";
  request_1.path_ = "/path_or_anything";
  request_1.protocol_ = "fake_fake_fake";
  request_1.data_ = {{"version", "v1"}, {"stream_id", "1"}, {"frame", "1_header"}};

  sendRequestForTest(request_1);

  waitForUpstreamConnectionForTest();
  const std::function<bool(const std::string&)> data_validator_1 =
      [](const std::string& data) -> bool {
    return data.find("frame:1_header") != std::string::npos;
  };
  waitForUpstreamRequestForTest(data_validator_1);

  FakeStreamCodecFactory::FakeRequest request_2;
  request_2.host_ = "service_name_0";
  request_2.method_ = "hello";
  request_2.path_ = "/path_or_anything";
  request_2.protocol_ = "fake_fake_fake";
  request_2.data_ = {{"version", "v1"}, {"stream_id", "2"}, {"frame", "2_header"}};

  // Reset request encoder callback.
  request_encoder_callback_ = std::make_shared<TestRequestEncoderCallback>();

  // Send the second request with the different stream id and expect the connection to be alive.
  sendRequestForTest(request_2);
  const std::function<bool(const std::string&)> data_validator_2 =
      [](const std::string& data) -> bool {
    return data.find("frame:2_header") != std::string::npos;
  };
  waitForUpstreamRequestForTest(data_validator_2);

  FakeStreamCodecFactory::FakeResponse response_2;
  response_2.protocol_ = "fake_fake_fake";
  response_2.status_ = Status();
  response_2.data_["zzzz"] = "xxxx";
  response_2.data_["stream_id"] = "2";

  sendResponseForTest(response_2);

  RELEASE_ASSERT(waitDownstreamResponseForTest(TestUtility::DefaultTimeout, 2),
                 "unexpected timeout");

  EXPECT_NE(response_decoder_callback_->responses_[2].response_, nullptr);
  EXPECT_EQ(response_decoder_callback_->responses_[2].response_->status().code(), StatusCode::kOk);
  EXPECT_EQ(response_decoder_callback_->responses_[2].response_->get("zzzz"), "xxxx");
  EXPECT_EQ(response_decoder_callback_->responses_[2].response_->get("stream_id"), "2");

  FakeStreamCodecFactory::FakeResponse response_1;
  response_1.protocol_ = "fake_fake_fake";
  response_1.status_ = Status();
  response_1.data_["zzzz"] = "yyyy";
  response_1.data_["stream_id"] = "1";

  sendResponseForTest(response_1);

  RELEASE_ASSERT(waitDownstreamResponseForTest(TestUtility::DefaultTimeout, 1),
                 "unexpected timeout");

  EXPECT_NE(response_decoder_callback_->responses_[1].response_, nullptr);
  EXPECT_EQ(response_decoder_callback_->responses_[1].response_->status().code(), StatusCode::kOk);
  EXPECT_EQ(response_decoder_callback_->responses_[1].response_->get("zzzz"), "yyyy");
  EXPECT_EQ(response_decoder_callback_->responses_[1].response_->get("stream_id"), "1");

  cleanup();
}

TEST_P(IntegrationTest, MultipleRequestsWithMultipleFrames) {
  FakeStreamCodecFactoryConfig codec_factory_config;
  Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

  auto codec_factory = std::make_unique<FakeStreamCodecFactory>();

  initialize(defaultConfig(true), std::move(codec_factory));

  EXPECT_TRUE(makeClientConnectionForTest());

  FakeStreamCodecFactory::FakeRequest request_1;
  request_1.host_ = "service_name_0";
  request_1.method_ = "hello";
  request_1.path_ = "/path_or_anything";
  request_1.protocol_ = "fake_fake_fake";
  request_1.data_ = {
      {"version", "v1"}, {"stream_id", "1"}, {"end_stream", "false"}, {"frame", "1_header"}};

  FakeStreamCodecFactory::FakeRequest request_1_frame_1;
  request_1_frame_1.data_ = {{"stream_id", "1"}, {"end_stream", "false"}, {"frame", "1_frame_1"}};

  FakeStreamCodecFactory::FakeRequest request_1_frame_2;
  request_1_frame_2.data_ = {{"stream_id", "1"}, {"end_stream", "true"}, {"frame", "1_frame_2"}};

  FakeStreamCodecFactory::FakeRequest request_2;
  request_2.host_ = "service_name_0";
  request_2.method_ = "hello";
  request_2.path_ = "/path_or_anything";
  request_2.protocol_ = "fake_fake_fake";
  request_2.data_ = {
      {"version", "v1"}, {"stream_id", "2"}, {"end_stream", "false"}, {"frame", "2_header"}};

  FakeStreamCodecFactory::FakeRequest request_2_frame_1;
  request_2_frame_1.data_ = {{"stream_id", "2"}, {"end_stream", "false"}, {"frame", "2_frame_1"}};

  FakeStreamCodecFactory::FakeRequest request_2_frame_2;
  request_2_frame_2.data_ = {{"stream_id", "2"}, {"end_stream", "true"}, {"frame", "2_frame_2"}};

  // We handle frame one by one to make sure the order is correct.

  sendRequestForTest(request_1);
  waitForUpstreamConnectionForTest();

  // First frame of request 1.
  const std::function<bool(const std::string&)> data_validator_1 =
      [](const std::string& data) -> bool {
    return data.find("frame:1_header") != std::string::npos;
  };
  waitForUpstreamRequestForTest(data_validator_1);

  // Second frame of request 1.
  sendRequestForTest(request_1_frame_1);
  const std::function<bool(const std::string&)> data_validator_1_frame_1 =
      [](const std::string& data) -> bool {
    return data.find("frame:1_frame_1") != std::string::npos;
  };
  waitForUpstreamRequestForTest(data_validator_1_frame_1);

  // First frame of request 2.
  sendRequestForTest(request_2);
  const std::function<bool(const std::string&)> data_validator_2 =
      [](const std::string& data) -> bool {
    return data.find("frame:2_header") != std::string::npos;
  };
  waitForUpstreamRequestForTest(data_validator_2);

  // Second frame of request 2.
  sendRequestForTest(request_2_frame_1);
  const std::function<bool(const std::string&)> data_validator_2_frame_1 =
      [](const std::string& data) -> bool {
    return data.find("frame:2_frame_1") != std::string::npos;
  };
  waitForUpstreamRequestForTest(data_validator_2_frame_1);

  // Third frame of request 1.
  sendRequestForTest(request_1_frame_2);
  const std::function<bool(const std::string&)> data_validator_1_frame_2 =
      [](const std::string& data) -> bool {
    return data.find("frame:1_frame_2") != std::string::npos;
  };
  waitForUpstreamRequestForTest(data_validator_1_frame_2);

  // Third frame of request 2.
  sendRequestForTest(request_2_frame_2);
  const std::function<bool(const std::string&)> data_validator_2_frame_2 =
      [](const std::string& data) -> bool {
    return data.find("frame:2_frame_2") != std::string::npos;
  };
  waitForUpstreamRequestForTest(data_validator_2_frame_2);

  FakeStreamCodecFactory::FakeResponse response_2;
  response_2.protocol_ = "fake_fake_fake";
  response_2.status_ = Status();
  response_2.data_["zzzz"] = "xxxx";
  response_2.data_["stream_id"] = "2";
  response_2.data_["end_stream"] = "false";

  FakeStreamCodecFactory::FakeResponse response_2_frame_1;
  response_2_frame_1.data_["stream_id"] = "2";
  response_2_frame_1.data_["end_stream"] = "true";

  sendResponseForTest(response_2);
  sendResponseForTest(response_2_frame_1);

  RELEASE_ASSERT(waitDownstreamResponseForTest(TestUtility::DefaultTimeout, 2),
                 "unexpected timeout");

  EXPECT_NE(response_decoder_callback_->responses_[2].response_, nullptr);
  EXPECT_EQ(response_decoder_callback_->responses_[2].response_->status().code(), StatusCode::kOk);
  EXPECT_EQ(response_decoder_callback_->responses_[2].response_->get("zzzz"), "xxxx");
  EXPECT_EQ(response_decoder_callback_->responses_[2].response_->get("stream_id"), "2");

  FakeStreamCodecFactory::FakeResponse response_1;
  response_1.protocol_ = "fake_fake_fake";
  response_1.status_ = Status();
  response_1.data_["zzzz"] = "yyyy";
  response_1.data_["stream_id"] = "1";
  response_1.data_["end_stream"] = "false";

  FakeStreamCodecFactory::FakeResponse response_1_frame_1;
  response_1_frame_1.data_["stream_id"] = "1";
  response_1_frame_1.data_["end_stream"] = "true";

  sendResponseForTest(response_1);
  sendResponseForTest(response_1_frame_1);

  RELEASE_ASSERT(waitDownstreamResponseForTest(TestUtility::DefaultTimeout, 1),
                 "unexpected timeout");

  EXPECT_NE(response_decoder_callback_->responses_[1].response_, nullptr);
  EXPECT_EQ(response_decoder_callback_->responses_[1].response_->status().code(), StatusCode::kOk);
  EXPECT_EQ(response_decoder_callback_->responses_[1].response_->get("zzzz"), "yyyy");
  EXPECT_EQ(response_decoder_callback_->responses_[1].response_->get("stream_id"), "1");

  cleanup();
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
