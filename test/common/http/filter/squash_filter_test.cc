#include <chrono>
#include <memory>

#include "common/config/filter_json.h"
#include "common/config/json_utility.h"
#include "common/http/filter/squash_filter.h"
#include "common/http/message_impl.h"
#include "common/protobuf/protobuf.h"

#include "server/config/http/squash.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::_;

using Envoy::Protobuf::util::MessageDifferencer;

namespace Envoy {
namespace Http {

namespace {
SquashFilterConfig constructSquashFilterConfigFromJson(
    const Envoy::Json::Object& json,
    NiceMock<Envoy::Server::Configuration::MockFactoryContext>& context) {
  envoy::api::v2::filter::http::Squash proto_config;
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

  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_EQ("fake_cluster", config.cluster_name());
  EXPECT_JSON_EQ("{\"a\":\"b\"}", config.attachment_json());
  EXPECT_EQ(std::chrono::milliseconds(1001), config.request_timeout());
  EXPECT_EQ(std::chrono::milliseconds(2002), config.attachment_poll_period());
  EXPECT_EQ(std::chrono::milliseconds(3003), config.attachment_timeout());
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

  EXPECT_THROW(constructSquashFilterConfigFromJson(*config, factory_context),
               Envoy::EnvoyException);
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
  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_JSON_EQ(expected_json, config.attachment_json());
}

TEST(SoloFilterConfigTest, ParsesAndEscapesEnvironment) {
  ::setenv("ESCAPE_ENV", "\"", 1);

  std::string json = R"EOF(
    {
      "cluster" : "squash",
      "attachment_template" : {"a":"{{ ESCAPE_ENV }}"}
    }
    )EOF";

  std::string expected_json = "{\"a\":\"\\\"\"}";

  Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_JSON_EQ(expected_json, config.attachment_json());
}

TEST(SoloFilterConfigTest, ParsesEnvironmentInComplexTemplate) {
  ::setenv("CONF_ENV", "some-config-value", 1);

  std::string json = R"EOF(
    {
      "cluster" : "squash",
      "attachment_template" : {"a":[{"e": "{{ CONF_ENV }}"},{"c":"d"}]}
    }
    )EOF";

  std::string expected_json = R"EOF({"a":[{"e": "some-config-value"},{"c":"d"}]})EOF";

  Envoy::Json::ObjectSharedPtr json_config = Envoy::Json::Factory::loadFromString(json);
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context;
  auto config = constructSquashFilterConfigFromJson(*json_config, factory_context);
  EXPECT_JSON_EQ(expected_json, config.attachment_json());
}

class SquashFilterTest : public testing::Test {
public:
  SquashFilterTest() : request_(&cm_.async_client_) {}

protected:
  void SetUp() override {}

  void initFilter() {
    envoy::api::v2::filter::http::Squash p;
    p.set_cluster("squash");
    config_ = std::make_shared<SquashFilterConfig>(p, factory_context_.cluster_manager_);

    filter_ = std::make_shared<SquashFilter>(config_, cm_);
    filter_->setDecoderFilterCallbacks(filter_callbacks_);
  }

  Envoy::Http::AsyncClient::Callbacks* startRequest() {
    initFilter();

    attachment_timeout_timer_ =
        new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);

    EXPECT_CALL(cm_, httpAsyncClientForCluster("squash"))
        .WillRepeatedly(ReturnRef(cm_.async_client_));

    Envoy::Http::AsyncClient::Callbacks* callbacks;

    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks& cb,
                             const Envoy::Optional<std::chrono::milliseconds>&)
                             -> Envoy::Http::AsyncClient::Request* {
          callbacks = &cb;
          return &request_;
        }));

    EXPECT_CALL(*attachment_timeout_timer_, enableTimer(config_->attachment_timeout()));

    Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"},
                                           {":authority", "www.solo.io"},
                                           {"x-squash-debug", "true"},
                                           {":path", "/getsomething"}};
    EXPECT_EQ(Envoy::Http::FilterHeadersStatus::StopIteration,
              filter_->decodeHeaders(headers, false));
    return callbacks;
  }

  Envoy::Http::AsyncClient::Callbacks* startCompleteRequest() {
    auto callbacks = startRequest();

    Envoy::Http::TestHeaderMapImpl trailers{};
    // Complete a full request cycle
    Envoy::Buffer::OwnedImpl buffer("nothing here");
    EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationAndBuffer,
              filter_->decodeData(buffer, false));
    EXPECT_EQ(Envoy::Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(trailers));

    return callbacks;
  }

  NiceMock<Envoy::Http::MockStreamDecoderFilterCallbacks> filter_callbacks_;
  NiceMock<Envoy::Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Envoy::Event::MockTimer>* attachment_timeout_timer_{};
  NiceMock<Envoy::Upstream::MockClusterManager> cm_;
  Envoy::Http::MockAsyncClientRequest request_;
  SquashFilterConfigSharedPtr config_;
  std::shared_ptr<SquashFilter> filter_;
};

TEST_F(SquashFilterTest, DecodeHeaderContinuesOnClientFail) {

  envoy::api::v2::filter::http::Squash p;
  p.set_cluster("squash");
  SquashFilterConfigSharedPtr config(new SquashFilterConfig(p, factory_context_.cluster_manager_));
  EXPECT_CALL(cm_, httpAsyncClientForCluster("squash")).WillOnce(ReturnRef(cm_.async_client_));

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks& callbacks,
                           const Envoy::Optional<std::chrono::milliseconds>&)
                           -> Envoy::Http::AsyncClient::Request* {
        callbacks.onFailure(Envoy::Http::AsyncClient::FailureReason::Reset);
        return nullptr;
      }));

  SquashFilter filter(config, cm_);

  Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"},
                                         {":authority", "www.solo.io"},
                                         {"x-squash-debug", "true"},
                                         {":path", "/getsomething"}};

  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter.decodeHeaders(headers, false));
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue, filter.decodeTrailers(headers));
}

TEST_F(SquashFilterTest, DecodeContinuesOnCreateAttachmentFail) {
  auto callbacks = startRequest();

  EXPECT_CALL(filter_callbacks_, continueDecoding());
  EXPECT_CALL(*attachment_timeout_timer_, disableTimer());
  callbacks->onFailure(Envoy::Http::AsyncClient::FailureReason::Reset);

  Envoy::Buffer::OwnedImpl data("nothing here");
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Envoy::Http::TestHeaderMapImpl trailers{};
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
}

TEST_F(SquashFilterTest, DoesNothingWithNoHeader) {

  envoy::api::v2::filter::http::Squash p;
  p.set_cluster("squash");
  SquashFilterConfigSharedPtr config(new SquashFilterConfig(p, factory_context_.cluster_manager_));
  EXPECT_CALL(cm_, httpAsyncClientForCluster(_)).Times(0);

  SquashFilter filter(config, cm_);

  Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"},
                                         {":authority", "www.solo.io"},
                                         {"x-not-squash-debug", "true"},
                                         {":path", "/getsomething"}};

  Envoy::Buffer::OwnedImpl data("nothing here");
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter.decodeHeaders(headers, false));
  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter.decodeData(data, false));
  EXPECT_EQ(Envoy::Http::FilterTrailersStatus::Continue, filter.decodeTrailers(headers));
}

TEST_F(SquashFilterTest, Timeout) {
  startRequest();

  // invoke timeout
  Envoy::Buffer::OwnedImpl buffer("nothing here");

  EXPECT_EQ(Envoy::Http::FilterDataStatus::StopIterationAndBuffer,
            filter_->decodeData(buffer, false));

  EXPECT_CALL(request_, cancel());
  EXPECT_CALL(filter_callbacks_, continueDecoding());

  attachment_timeout_timer_->callback_();

  EXPECT_EQ(Envoy::Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));
}

TEST_F(SquashFilterTest, HappyPathWithTrailers) {

  Envoy::Http::AsyncClient::Callbacks* callbacks = startCompleteRequest();

  {
    // Expect the get attachment request
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks& cb,
                             const Envoy::Optional<std::chrono::milliseconds>&)
                             -> Envoy::Http::AsyncClient::Request* {
          callbacks = &cb;
          return &request_;
        }));

    // return the create request
    Http::MessagePtr msg(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "201"}}}));
    msg->body().reset(new Buffer::OwnedImpl(R"EOF({"metadata":{"name":"a"}})EOF"));
    callbacks->onSuccess(std::move(msg));
  }
  {
    Http::MessagePtr msg(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
    msg->body().reset(new Buffer::OwnedImpl(R"EOF({"status":{"state":"attached"}})EOF"));

    EXPECT_CALL(filter_callbacks_, continueDecoding());

    callbacks->onSuccess(std::move(msg));
  }
}

TEST_F(SquashFilterTest, CheckRetryPollingAttachment) {
  Envoy::Http::AsyncClient::Callbacks* callbacks = startCompleteRequest();

  {
    // Expect the get attachment request
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks& cb,
                             const Envoy::Optional<std::chrono::milliseconds>&)
                             -> Envoy::Http::AsyncClient::Request* {
          callbacks = &cb;
          return &request_;
        }));

    // return the create request
    Http::MessagePtr msg(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "201"}}}));
    msg->body().reset(new Buffer::OwnedImpl(R"EOF({"metadata":{"name":"a"}})EOF"));
    callbacks->onSuccess(std::move(msg));
  }
  NiceMock<Envoy::Event::MockTimer>* retry_timer;
  {
    Http::MessagePtr msg(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
    msg->body().reset(new Buffer::OwnedImpl(R"EOF({"status":{"state":"attaching"}})EOF"));

    retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);

    EXPECT_CALL(*retry_timer, enableTimer(config_->attachment_poll_period()));
    callbacks->onSuccess(std::move(msg));
    callbacks = nullptr;
  }

  {
    // Expect the second get attachment request
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks& cb,
                             const Envoy::Optional<std::chrono::milliseconds>&)
                             -> Envoy::Http::AsyncClient::Request* {
          callbacks = &cb;
          return &request_;
        }));

    retry_timer->callback_();

    Http::MessagePtr msg(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
    msg->body().reset(new Buffer::OwnedImpl(R"EOF({"status":{"state":"attached"}})EOF"));

    EXPECT_CALL(filter_callbacks_, continueDecoding());
    callbacks->onSuccess(std::move(msg));
  }
}

TEST_F(SquashFilterTest, CheckRetryPollingAttachmentOnFailure) {
  Envoy::Http::AsyncClient::Callbacks* callbacks = startCompleteRequest();

  {
    // Expect the get attachment request
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks& cb,
                             const Envoy::Optional<std::chrono::milliseconds>&)
                             -> Envoy::Http::AsyncClient::Request* {
          callbacks = &cb;
          return &request_;
        }));

    // return the create request
    Http::MessagePtr msg(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "201"}}}));
    msg->body().reset(new Buffer::OwnedImpl(R"EOF({"metadata":{"name":"a"}})EOF"));
    callbacks->onSuccess(std::move(msg));
  }
  NiceMock<Envoy::Event::MockTimer>* retry_timer;
  {
    retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);
    EXPECT_CALL(*retry_timer, enableTimer(config_->attachment_poll_period()));
    callbacks->onFailure(Envoy::Http::AsyncClient::FailureReason::Reset);
    callbacks = nullptr;
  }

  {
    // Expect the second get attachment request
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks& cb,
                             const Envoy::Optional<std::chrono::milliseconds>&)
                             -> Envoy::Http::AsyncClient::Request* {
          callbacks = &cb;
          return &request_;
        }));

    retry_timer->callback_();

    Http::MessagePtr msg(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
    msg->body().reset(new Buffer::OwnedImpl(R"EOF({"status":{"state":"attached"}})EOF"));

    EXPECT_CALL(filter_callbacks_, continueDecoding());
    callbacks->onSuccess(std::move(msg));
  }
}

TEST_F(SquashFilterTest, DestroyedInTheMiddle) {
  Envoy::Http::AsyncClient::Callbacks* callbacks = startCompleteRequest();

  {
    // Expect the get attachment request
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks& cb,
                             const Envoy::Optional<std::chrono::milliseconds>&)
                             -> Envoy::Http::AsyncClient::Request* {
          callbacks = &cb;
          return &request_;
        }));

    // return the create request
    Http::MessagePtr msg(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "201"}}}));
    msg->body().reset(new Buffer::OwnedImpl(R"EOF({"metadata":{"name":"a"}})EOF"));
    callbacks->onSuccess(std::move(msg));
  }
  NiceMock<Envoy::Event::MockTimer>* retry_timer;
  {
    Http::MessagePtr msg(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
    msg->body().reset(new Buffer::OwnedImpl(R"EOF({"status":{"state":"attaching"}})EOF"));

    retry_timer = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);

    EXPECT_CALL(*retry_timer, enableTimer(config_->attachment_poll_period()));
    callbacks->onSuccess(std::move(msg));
    callbacks = nullptr;
  }

  EXPECT_CALL(*attachment_timeout_timer_, disableTimer());
  EXPECT_CALL(*retry_timer, disableTimer());

  filter_->onDestroy();
}

TEST_F(SquashFilterTest, DestroyedInFlight) {
  startCompleteRequest();

  EXPECT_CALL(request_, cancel());
  EXPECT_CALL(*attachment_timeout_timer_, disableTimer());

  filter_->onDestroy();
}

TEST_F(SquashFilterTest, TimerExpiresInline) {
  initFilter();

  attachment_timeout_timer_ = new NiceMock<Envoy::Event::MockTimer>(&filter_callbacks_.dispatcher_);
  EXPECT_CALL(*attachment_timeout_timer_, enableTimer(config_->attachment_timeout()))
      .WillOnce(Invoke([&](const std::chrono::milliseconds&) {
        // timer expires inline
        attachment_timeout_timer_->callback_();
      }));

  EXPECT_CALL(cm_.async_client_, send_(_, _, _))
      .WillOnce(Invoke([&](Envoy::Http::MessagePtr&, Envoy::Http::AsyncClient::Callbacks&,
                           const Envoy::Optional<std::chrono::milliseconds>&)
                           -> Envoy::Http::AsyncClient::Request* { return &request_; }));

  EXPECT_CALL(request_, cancel());
  Envoy::Http::TestHeaderMapImpl headers{{":method", "GET"},
                                         {":authority", "www.solo.io"},
                                         {"x-squash-debug", "true"},
                                         {":path", "/getsomething"}};
  EXPECT_EQ(Envoy::Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

} // namespace Http
} // namespace Envoy
