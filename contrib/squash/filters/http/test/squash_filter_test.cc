#include <chrono>
#include <memory>
#include <string>

#include "source/common/http/message_impl.h"
#include "source/common/protobuf/protobuf.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "contrib/envoy/extensions/filters/http/squash/v3/squash.pb.h"
#include "contrib/squash/filters/http/source/squash_filter.h"
#include "fmt/format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

using Envoy::Protobuf::util::MessageDifferencer;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Squash {
namespace {

SquashFilterConfig constructSquashFilterConfigFromYaml(
    const std::string& yaml, NiceMock<Envoy::Server::Configuration::MockFactoryContext>& context) {
  envoy::extensions::filters::http::squash::v3::Squash proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  return {proto_config, context.cluster_manager_};
}

void expectJsonEq(const std::string& expected, const std::string& actual) {
  ProtobufWkt::Struct actualjson;
  TestUtility::loadFromJson(actual, actualjson);

  ProtobufWkt::Struct expectedjson;
  TestUtility::loadFromJson(expected, expectedjson);

  EXPECT_TRUE(MessageDifferencer::Equals(expectedjson, actualjson));
}

} // namespace

TEST(SquashFilterConfigTest, V2ApiConversion) {
  const std::string yaml = R"EOF(
  cluster: fake_cluster
  attachment_template:
    a: b
  request_timeout: 1.001s
  attachment_poll_period: 2.002s
  attachment_timeout: 3.003s
  )EOF";

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  factory_context.cluster_manager_.initializeClusters({"fake_cluster"}, {});

  const auto config = constructSquashFilterConfigFromYaml(yaml, factory_context);
  EXPECT_EQ("fake_cluster", config.clusterName());
  expectJsonEq("{\"a\":\"b\"}", config.attachmentJson());
  EXPECT_EQ(std::chrono::milliseconds(1001), config.requestTimeout());
  EXPECT_EQ(std::chrono::milliseconds(2002), config.attachmentPollPeriod());
  EXPECT_EQ(std::chrono::milliseconds(3003), config.attachmentTimeout());
}

TEST(SquashFilterConfigTest, NoCluster) {
  const std::string yaml = R"EOF(
  cluster: fake_cluster
  attachment_template: {}
  )EOF";

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW_WITH_MESSAGE(constructSquashFilterConfigFromYaml(yaml, factory_context),
                            Envoy::EnvoyException,
                            "squash filter: unknown cluster 'fake_cluster' in squash config");
}

TEST(SquashFilterConfigTest, ParsesEnvironment) {
  const std::string yaml = R"EOF(
  cluster: squash
  attachment_template:
    a: "{{ MISSING_ENV }}"

  )EOF";
  const std::string expected_json = "{\"a\":\"\"}";

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  factory_context.cluster_manager_.initializeClusters({"squash"}, {});

  const auto config = constructSquashFilterConfigFromYaml(yaml, factory_context);
  expectJsonEq(expected_json, config.attachmentJson());
}

TEST(SquashFilterConfigTest, ParsesAndEscapesEnvironment) {
  TestEnvironment::setEnvVar("ESCAPE_ENV", "\"", 1);

  const std::string yaml = R"EOF(
  cluster: squash
  attachment_template:
    a: "{{ ESCAPE_ENV }}"
  )EOF";

  const std::string expected_json = "{\"a\":\"\\\"\"}";

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  factory_context.cluster_manager_.initializeClusters({"squash"}, {});
  const auto config = constructSquashFilterConfigFromYaml(yaml, factory_context);
  expectJsonEq(expected_json, config.attachmentJson());
}

TEST(SquashFilterConfigTest, TwoEnvironmentVariables) {
  TestEnvironment::setEnvVar("ENV1", "1", 1);
  TestEnvironment::setEnvVar("ENV2", "2", 1);

  const std::string yaml = R"EOF(
  cluster: squash
  attachment_template:
    a: "{{ ENV1 }}-{{ ENV2 }}"
  )EOF";

  const std::string expected_json = "{\"a\":\"1-2\"}";

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  factory_context.cluster_manager_.initializeClusters({"squash"}, {});
  auto config = constructSquashFilterConfigFromYaml(yaml, factory_context);
  expectJsonEq(expected_json, config.attachmentJson());
}

TEST(SquashFilterConfigTest, ParsesEnvironmentInComplexTemplate) {
  TestEnvironment::setEnvVar("CONF_ENV", "some-config-value", 1);

  const std::string yaml = R"EOF(
  cluster: squash
  attachment_template:
    a:
    - e: "{{ CONF_ENV }}"
    - c: d
  )EOF";

  const std::string expected_json = R"EOF({"a":[{"e": "some-config-value"},{"c":"d"}]})EOF";

  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  factory_context.cluster_manager_.initializeClusters({"squash"}, {});
  const auto config = constructSquashFilterConfigFromYaml(yaml, factory_context);
  expectJsonEq(expected_json, config.attachmentJson());
}

class SquashFilterTest : public testing::Test {
public:
  SquashFilterTest()
      : request_(&factory_context_.cluster_manager_.thread_local_cluster_.async_client_) {}

protected:
  void SetUp() override {}

  void initFilter() {
    envoy::extensions::filters::http::squash::v3::Squash p;
    p.set_cluster("squash");
    factory_context_.cluster_manager_.initializeClusters({"squash"}, {});
    factory_context_.cluster_manager_.initializeThreadLocalClusters({"squash"});
    config_ = std::make_shared<SquashFilterConfig>(p, factory_context_.cluster_manager_);

    filter_ = std::make_shared<SquashFilter>(config_, factory_context_.cluster_manager_);
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  // start a downstream request marked with the squash header.
  // note that a side effect of this is that
  // a call to the squash server will be made.
  // use popPendingCallback() to reply to that call.
  void startDownstreamRequest() {
    initFilter();

    attachmentTimeout_timer_ =
        new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);

    EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_, httpAsyncClient())
        .WillRepeatedly(
            ReturnRef(factory_context_.cluster_manager_.thread_local_cluster_.async_client_));

    expectAsyncClientSend();

    EXPECT_CALL(*attachmentTimeout_timer_, enableTimer(config_->attachmentTimeout(), _));

    Envoy::Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                                  {":authority", "www.solo.io"},
                                                  {"x-squash-debug", "true"},
                                                  {":path", "/getsomething"}};
    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(headers, false));
  }

  void doDownstreamRequest() {
    startDownstreamRequest();

    Http::MetadataMap metadata_map{{"metadata", "metadata"}};
    EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
    Http::TestRequestTrailerMapImpl trailers;
    // Complete a full request cycle
    Envoy::Buffer::OwnedImpl buffer("nothing here");
    EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationAndBuffer,
              filter_->decodeData(buffer, false));
    EXPECT_EQ(Envoy::Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(trailers));
  }

  void expectAsyncClientSend() {
    EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.async_client_,
                send_(_, _, _))
        .WillOnce(Invoke(
            [&](Envoy::Http::RequestMessagePtr&, Envoy::Http::AsyncClient::Callbacks& cb,
                const Http::AsyncClient::RequestOptions&) -> Envoy::Http::AsyncClient::Request* {
              callbacks_.push_back(&cb);
              return &request_;
            }));
  }

  void completeRequest(const std::string& status, const std::string& body) {
    Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(
        Http::ResponseHeaderMapPtr{new Http::TestResponseHeaderMapImpl{{":status", status}}}));
    msg->body().add(body);
    popPendingCallback()->onSuccess(request_, std::move(msg));
  }

  void completeCreateRequest() {
    // return the create request
    completeRequest("201", R"EOF({"metadata":{"name":"a"}})EOF");
  }

  void completeGetStatusRequest(const std::string& status) {
    completeRequest("200", fmt::format(R"EOF({{"status":{{"state":"{}"}}}})EOF", status));
  }

  Envoy::Http::AsyncClient::Callbacks* popPendingCallback() {
    if (callbacks_.empty()) {
      // Can't use ASSERT_* as this is not a test function
      throw std::underflow_error("empty deque");
    }

    auto callbacks = callbacks_.front();
    callbacks_.pop_front();
    return callbacks;
  }

  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Envoy::Event::MockTimer>* attachmentTimeout_timer_{};
  Envoy::Http::MockAsyncClientRequest request_;
  SquashFilterConfigSharedPtr config_;
  std::shared_ptr<SquashFilter> filter_;
  std::deque<Envoy::Http::AsyncClient::Callbacks*> callbacks_;
};

TEST_F(SquashFilterTest, DecodeHeaderContinuesOnClientFail) {
  initFilter();

  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_, httpAsyncClient())
      .WillOnce(ReturnRef(factory_context_.cluster_manager_.thread_local_cluster_.async_client_));

  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Envoy::Http::RequestMessagePtr&, Envoy::Http::AsyncClient::Callbacks& callbacks,
              const Http::AsyncClient::RequestOptions&) -> Envoy::Http::AsyncClient::Request* {
            callbacks.onFailure(request_, Envoy::Http::AsyncClient::FailureReason::Reset);
            // Intentionally return nullptr (instead of request handle) to trigger a particular
            // code path.
            return nullptr;
          }));

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":authority", "www.solo.io"},
                                         {"x-squash-debug", "true"},
                                         {":path", "/getsomething"}};

  Envoy::Buffer::OwnedImpl data("nothing here");
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
}

TEST_F(SquashFilterTest, DecodeContinuesOnCreateAttachmentFail) {
  startDownstreamRequest();

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(*attachmentTimeout_timer_, disableTimer());
  popPendingCallback()->onFailure(request_, Envoy::Http::AsyncClient::FailureReason::Reset);

  Envoy::Buffer::OwnedImpl data("nothing here");
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
}

TEST_F(SquashFilterTest, DoesNothingWithNoHeader) {
  initFilter();
  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_, httpAsyncClient()).Times(0);

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":authority", "www.solo.io"},
                                         {"x-not-squash-debug", "true"},
                                         {":path", "/getsomething"}};

  Envoy::Buffer::OwnedImpl data("nothing here");
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
}

TEST_F(SquashFilterTest, Timeout) {
  startDownstreamRequest();

  // invoke timeout
  Envoy::Buffer::OwnedImpl buffer("nothing here");

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->decodeData(buffer, false));

  EXPECT_CALL(request_, cancel());
  EXPECT_CALL(filter_callbacks_, continueDecoding());

  EXPECT_CALL(filter_callbacks_.dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(filter_callbacks_.dispatcher_, popTrackedObject(_));
  attachmentTimeout_timer_->invokeCallback();

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
}

TEST_F(SquashFilterTest, HappyPathWithTrailers) {
  doDownstreamRequest();
  // Expect the get attachment request
  expectAsyncClientSend();
  completeCreateRequest();

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  completeGetStatusRequest("attached");
}

TEST_F(SquashFilterTest, CheckRetryPollingAttachment) {
  doDownstreamRequest();
  // Expect the get attachment request
  expectAsyncClientSend();
  completeCreateRequest();

  auto retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);

  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod(), _));
  completeGetStatusRequest("attaching");

  // Expect the second get attachment request
  expectAsyncClientSend();
  EXPECT_CALL(filter_callbacks_.dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(filter_callbacks_.dispatcher_, popTrackedObject(_));

  retry_timer->invokeCallback();
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  completeGetStatusRequest("attached");
}

TEST_F(SquashFilterTest, PollingAttachmentNoCluster) {
  doDownstreamRequest();
  // Expect the get attachment request
  expectAsyncClientSend();
  completeCreateRequest();

  auto retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);

  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod(), _));
  completeGetStatusRequest("attaching");

  // Expect the second get attachment request
  ON_CALL(factory_context_.cluster_manager_, getThreadLocalCluster("squash"))
      .WillByDefault(Return(nullptr));
  EXPECT_CALL(filter_callbacks_.dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(filter_callbacks_.dispatcher_, popTrackedObject(_));
  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod(), _));
  retry_timer->invokeCallback();
}

TEST_F(SquashFilterTest, CheckRetryPollingAttachmentOnFailure) {
  doDownstreamRequest();
  // Expect the first get attachment request
  expectAsyncClientSend();
  completeCreateRequest();

  auto retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);
  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod(), _));
  popPendingCallback()->onFailure(request_, Envoy::Http::AsyncClient::FailureReason::Reset);

  // Expect the second get attachment request
  expectAsyncClientSend();

  EXPECT_CALL(filter_callbacks_.dispatcher_, pushTrackedObject(_));
  EXPECT_CALL(filter_callbacks_.dispatcher_, popTrackedObject(_));
  retry_timer->invokeCallback();

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  completeGetStatusRequest("attached");
}

TEST_F(SquashFilterTest, DestroyedInTheMiddle) {
  doDownstreamRequest();
  // Expect the get attachment request
  expectAsyncClientSend();
  completeCreateRequest();

  auto retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);
  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod(), _));
  completeGetStatusRequest("attaching");

  EXPECT_CALL(*attachmentTimeout_timer_, disableTimer());
  EXPECT_CALL(*retry_timer, disableTimer());

  filter_->onDestroy();
}

TEST_F(SquashFilterTest, InvalidJsonForCreateAttachment) {
  doDownstreamRequest();
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  completeRequest("201", "This is not a JSON object");
}

TEST_F(SquashFilterTest, InvalidJsonForGetAttachment) {
  doDownstreamRequest();
  // Expect the get attachment request
  expectAsyncClientSend();
  completeCreateRequest();

  auto retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);
  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod(), _));
  completeRequest("200", "This is not a JSON object");
}

TEST_F(SquashFilterTest, InvalidResponseWithNoBody) {
  doDownstreamRequest();
  // Expect the get attachment request
  expectAsyncClientSend();
  completeCreateRequest();

  auto retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);
  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod(), _));
  Http::ResponseMessagePtr msg(new Http::ResponseMessageImpl(Http::ResponseHeaderMapPtr{
      new Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-length", "0"}}}));
  popPendingCallback()->onSuccess(request_, std::move(msg));
}

TEST_F(SquashFilterTest, DestroyedInFlight) {
  doDownstreamRequest();

  EXPECT_CALL(request_, cancel());
  EXPECT_CALL(*attachmentTimeout_timer_, disableTimer());

  filter_->onDestroy();
}

TEST_F(SquashFilterTest, TimerExpiresInline) {
  initFilter();

  attachmentTimeout_timer_ = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);
  EXPECT_CALL(*attachmentTimeout_timer_, enableTimer(config_->attachmentTimeout(), _))
      .WillOnce(Invoke([&](const std::chrono::milliseconds&, const ScopeTrackedObject* scope) {
        attachmentTimeout_timer_->scope_ = scope;
        attachmentTimeout_timer_->enabled_ = true;
        // timer expires inline
        EXPECT_CALL(filter_callbacks_.dispatcher_, pushTrackedObject(_));
        EXPECT_CALL(filter_callbacks_.dispatcher_, popTrackedObject(_));
        attachmentTimeout_timer_->invokeCallback();
      }));

  EXPECT_CALL(factory_context_.cluster_manager_.thread_local_cluster_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([&](Envoy::Http::RequestMessagePtr&, Envoy::Http::AsyncClient::Callbacks&,
                           const Http::AsyncClient::RequestOptions&)
                           -> Envoy::Http::AsyncClient::Request* { return &request_; }));

  EXPECT_CALL(request_, cancel());
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {":authority", "www.solo.io"},
                                         {"x-squash-debug", "true"},
                                         {":path", "/getsomething"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
