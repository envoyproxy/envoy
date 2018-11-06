#include <chrono>
#include <memory>

#include "common/config/filter_json.h"
#include "common/config/json_utility.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/squash/squash_filter.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

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
SquashFilterConfig constructSquashFilterConfigFromJson(
    const Envoy::Json::Object& json,
    NiceMock<Envoy::Server::Configuration::MockFactoryContext>& context) {
  envoy::config::filter::http::squash::v2::Squash proto_config;
  Config::FilterJson::translateSquashConfig(json, proto_config);
  return SquashFilterConfig(proto_config, context.cluster_manager_);
}

void EXPECT_JSON_EQ(const std::string& expected, const std::string& actual) {
  ProtobufWkt::Struct actualjson;
  MessageUtil::loadFromJson(actual, actualjson);

  ProtobufWkt::Struct expectedjson;
  MessageUtil::loadFromJson(expected, expectedjson);

  EXPECT_TRUE(MessageDifferencer::Equals(expectedjson, actualjson));
}

} // namespace

TEST(SoloFilterConfigTest, V1ApiConversion) {
  std::string json = R"EOF(
    {
      "cluster" : "fake_cluster",
      "attachment_template" : {"a":"b"},
      "request_timeout_ms" : 1001,
      "attachment_poll_period_ms" : 2002,
      "attachment_timeout_ms" : 3003
    }
    )EOF";

  Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_CALL(factory_context.cluster_manager_, get("fake_cluster")).Times(1);

  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_EQ("fake_cluster", config.clusterName());
  EXPECT_JSON_EQ("{\"a\":\"b\"}", config.attachmentJson());
  EXPECT_EQ(std::chrono::milliseconds(1001), config.requestTimeout());
  EXPECT_EQ(std::chrono::milliseconds(2002), config.attachmentPollPeriod());
  EXPECT_EQ(std::chrono::milliseconds(3003), config.attachmentTimeout());
}

TEST(SoloFilterConfigTest, NoCluster) {
  std::string json = R"EOF(
    {
      "cluster" : "fake_cluster",
      "attachment_template" : {}
    }
    )EOF";

  Envoy::Json::ObjectSharedPtr config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;

  EXPECT_CALL(factory_context.cluster_manager_, get("fake_cluster")).WillOnce(Return(nullptr));

  EXPECT_THROW_WITH_MESSAGE(constructSquashFilterConfigFromJson(*config, factory_context),
                            Envoy::EnvoyException,
                            "squash filter: unknown cluster 'fake_cluster' in squash config");
}

TEST(SoloFilterConfigTest, ParsesEnvironment) {
  std::string json = R"EOF(
    {
      "cluster" : "squash",
      "attachment_template" : {"a":"{{ MISSING_ENV }}"}
    }
    )EOF";
  std::string expected_json = "{\"a\":\"\"}";

  Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_CALL(factory_context.cluster_manager_, get("squash")).Times(1);

  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_JSON_EQ(expected_json, config.attachmentJson());
}

TEST(SoloFilterConfigTest, ParsesAndEscapesEnvironment) {
  TestEnvironment::setEnvVar("ESCAPE_ENV", "\"", 1);

  std::string json = R"EOF(
    {
      "cluster" : "squash",
      "attachment_template" : {"a":"{{ ESCAPE_ENV }}"}
    }
    )EOF";

  std::string expected_json = "{\"a\":\"\\\"\"}";

  Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_CALL(factory_context.cluster_manager_, get("squash")).Times(1);
  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_JSON_EQ(expected_json, config.attachmentJson());
}
TEST(SoloFilterConfigTest, TwoEnvironmentVariables) {
  TestEnvironment::setEnvVar("ENV1", "1", 1);
  TestEnvironment::setEnvVar("ENV2", "2", 1);

  std::string json = R"EOF(
    {
      "cluster" : "squash",
      "attachment_template" : {"a":"{{ ENV1 }}-{{ ENV2 }}"}
    }
    )EOF";

  std::string expected_json = "{\"a\":\"1-2\"}";

  Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_JSON_EQ(expected_json, config.attachmentJson());
}

TEST(SoloFilterConfigTest, ParsesEnvironmentInComplexTemplate) {
  TestEnvironment::setEnvVar("CONF_ENV", "some-config-value", 1);

  std::string json = R"EOF(
    {
      "cluster" : "squash",
      "attachment_template" : {"a":[{"e": "{{ CONF_ENV }}"},{"c":"d"}]}
    }
    )EOF";

  std::string expected_json = R"EOF({"a":[{"e": "some-config-value"},{"c":"d"}]})EOF";

  Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_CALL(factory_context.cluster_manager_, get("squash")).Times(1);
  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_JSON_EQ(expected_json, config.attachmentJson());
}

class SquashFilterTest : public testing::Test {
public:
  SquashFilterTest() : request_(&cm_.async_client_) {}

protected:
  void SetUp() override {}

  void initFilter() {
    envoy::config::filter::http::squash::v2::Squash p;
    p.set_cluster("squash");
    config_ = std::make_shared<SquashFilterConfig>(p, factory_context_.cluster_manager_);

    filter_ = std::make_shared<SquashFilter>(config_, cm_);
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

    EXPECT_CALL(cm_, httpAsyncClientForCluster("squash"))
        .WillRepeatedly(ReturnRef(cm_.async_client_));

    expectAsyncClientSend();

    EXPECT_CALL(*attachmentTimeout_timer_, enableTimer(config_->attachmentTimeout()));

    Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"},
                                           {":authority", "www.solo.io"},
                                           {"x-squash-debug", "true"},
                                           {":path", "/getsomething"}};
    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(headers, false));
  }

  void doDownstreamRequest() {
    startDownstreamRequest();

    Envoy::Http::TestHeaderMapImpl trailers{};
    // Complete a full request cycle
    Envoy::Buffer::OwnedImpl buffer("nothing here");
    EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationAndBuffer,
              filter_->decodeData(buffer, false));
    EXPECT_EQ(Envoy::Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(trailers));
  }

  void expectAsyncClientSend() {
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks& cb,
                             const absl::optional<std::chrono::milliseconds>&)
                             -> Envoy::Http::AsyncClient::Request* {
          callbacks_.push_back(&cb);
          return &request_;
        }));
  }

  void completeRequest(const std::string& status, const std::string& body) {
    Http::MessagePtr msg(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", status}}}));
    msg->body() = std::make_unique<Buffer::OwnedImpl>(body);
    popPendingCallback()->onSuccess(std::move(msg));
  }

  void completeCreateRequest() {
    // return the create request
    completeRequest("201", R"EOF({"metadata":{"name":"a"}})EOF");
  }

  void completeGetStatusRequest(const std::string& status) {
    completeRequest("200", fmt::format(R"EOF({{"status":{{"state":"{}"}}}})EOF", status));
  }

  Envoy::Http::AsyncClient::Callbacks* popPendingCallback() {
    if (0 == callbacks_.size()) {
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
  NiceMock<Envoy::Upstream::MockClusterManager> cm_;
  Envoy::Http::MockAsyncClientRequest request_;
  SquashFilterConfigSharedPtr config_;
  std::shared_ptr<SquashFilter> filter_;
  std::deque<Envoy::Http::AsyncClient::Callbacks*> callbacks_;
};

TEST_F(SquashFilterTest, DecodeHeaderContinuesOnClientFail) {
  initFilter();

  EXPECT_CALL(cm_, httpAsyncClientForCluster("squash")).WillOnce(ReturnRef(cm_.async_client_));

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks& callbacks,
                           const absl::optional<std::chrono::milliseconds>&)
                           -> Envoy::Http::AsyncClient::Request* {
        callbacks.onFailure(Envoy::Http::AsyncClient::FailureReason::Reset);
        return nullptr;
      }));

  Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"},
                                         {":authority", "www.solo.io"},
                                         {"x-squash-debug", "true"},
                                         {":path", "/getsomething"}};

  Envoy::Buffer::OwnedImpl data("nothing here");
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

TEST_F(SquashFilterTest, DecodeContinuesOnCreateAttachmentFail) {
  startDownstreamRequest();

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(*attachmentTimeout_timer_, disableTimer());
  popPendingCallback()->onFailure(Envoy::Http::AsyncClient::FailureReason::Reset);

  Envoy::Buffer::OwnedImpl data("nothing here");
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Envoy::Http::TestHeaderMapImpl trailers{};
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
}

TEST_F(SquashFilterTest, DoesNothingWithNoHeader) {
  initFilter();
  EXPECT_CALL(cm_, httpAsyncClientForCluster(_)).Times(0);

  Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"},
                                         {":authority", "www.solo.io"},
                                         {"x-not-squash-debug", "true"},
                                         {":path", "/getsomething"}};

  Envoy::Buffer::OwnedImpl data("nothing here");
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(headers));
}

TEST_F(SquashFilterTest, Timeout) {
  startDownstreamRequest();

  // invoke timeout
  Envoy::Buffer::OwnedImpl buffer("nothing here");

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->decodeData(buffer, false));

  EXPECT_CALL(request_, cancel());
  EXPECT_CALL(filter_callbacks_, continueDecoding());

  attachmentTimeout_timer_->callback_();

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

  NiceMock<Envoy::Event::MockTimer>* retry_timer;
  retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);

  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod()));
  completeGetStatusRequest("attaching");

  // Expect the second get attachment request
  expectAsyncClientSend();
  retry_timer->callback_();
  EXPECT_CALL(filter_callbacks_, continueDecoding());
  completeGetStatusRequest("attached");
}

TEST_F(SquashFilterTest, CheckRetryPollingAttachmentOnFailure) {
  doDownstreamRequest();
  // Expect the first get attachment request
  expectAsyncClientSend();
  completeCreateRequest();

  NiceMock<Envoy::Event::MockTimer>* retry_timer;
  retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);
  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod()));
  popPendingCallback()->onFailure(Envoy::Http::AsyncClient::FailureReason::Reset);

  // Expect the second get attachment request
  expectAsyncClientSend();

  retry_timer->callback_();

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  completeGetStatusRequest("attached");
}

TEST_F(SquashFilterTest, DestroyedInTheMiddle) {
  doDownstreamRequest();
  // Expect the get attachment request
  expectAsyncClientSend();
  completeCreateRequest();

  auto retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);
  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod()));
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
  EXPECT_CALL(*retry_timer, enableTimer(config_->attachmentPollPeriod()));
  completeRequest("200", "This is not a JSON object");
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
  EXPECT_CALL(*attachmentTimeout_timer_, enableTimer(config_->attachmentTimeout()))
      .WillOnce(Invoke([&](const std::chrono::milliseconds&) {
        // timer expires inline
        attachmentTimeout_timer_->callback_();
      }));

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks&,
                           const absl::optional<std::chrono::milliseconds>&)
                           -> Envoy::Http::AsyncClient::Request* { return &request_; }));

  EXPECT_CALL(request_, cancel());
  Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"},
                                         {":authority", "www.solo.io"},
                                         {"x-squash-debug", "true"},
                                         {":path", "/getsomething"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

} // namespace Squash
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
