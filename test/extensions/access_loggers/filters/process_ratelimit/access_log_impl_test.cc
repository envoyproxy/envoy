#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/config/accesslog/v3/accesslog.pb.h"
#include "envoy/config/accesslog/v3/accesslog.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/scope.h"
#include "envoy/type/v3/token_bucket.pb.h"
#include "envoy/type/v3/token_bucket.pb.validate.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/protobuf/message_validator_impl.h"

#include "test/common/stream_info/test_util.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/config/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace ProcessRateLimit {
namespace {

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

envoy::config::accesslog::v3::AccessLog parseAccessLogFromV3Yaml(const std::string& yaml) {
  envoy::config::accesslog::v3::AccessLog access_log;
  TestUtility::loadFromYamlAndValidate(yaml, access_log);
  return access_log;
}

class AccessLogImplTestWithRateLimitFilter : public Event::TestUsingSimulatedTime,
                                             public testing::Test {
public:
  AccessLogImplTestWithRateLimitFilter()
      : stream_info_(time_source_), file_(new AccessLog::MockAccessLogFile()) {
    ON_CALL(context_.server_factory_context_, runtime()).WillByDefault(ReturnRef(runtime_));
    ON_CALL(context_.server_factory_context_, accessLogManager())
        .WillByDefault(ReturnRef(log_manager_));
    ON_CALL(log_manager_, createAccessLog(_)).WillByDefault(Return(file_));
    ON_CALL(context_.server_factory_context_, scope()).WillByDefault(ReturnRef(context_.scope()));
    ON_CALL(*file_, write(_)).WillByDefault(SaveArg<0>(&output_));
    stream_info_.addBytesReceived(1);
    stream_info_.addBytesSent(2);
    stream_info_.protocol(Http::Protocol::Http11);
    // Clear default stream id provider.
    stream_info_.stream_id_provider_ = nullptr;
    time_system_ = new Envoy::Event::SimulatedTimeSystem();
    context_.server_factory_context_.dispatcher_.time_system_.reset(time_system_);

    ON_CALL(context_.server_factory_context_.xds_manager_,
            subscribeToSingletonResource(_, _, _, _, _, _, _))
        .WillByDefault(Invoke(
            [this](absl::string_view resource_name,
                   OptRef<const envoy::config::core::v3::ConfigSource>, absl::string_view,
                   Stats::Scope&, Config::SubscriptionCallbacks& callbacks,
                   Config::OpaqueResourceDecoderSharedPtr,
                   const Config::SubscriptionOptions&) -> absl::StatusOr<Config::SubscriptionPtr> {
              auto ret = std::make_unique<testing::NiceMock<Config::MockSubscription>>();
              subscriptions_[resource_name] = ret.get();
              callbackss_[resource_name] = &callbacks;
              return ret;
            }));

    ON_CALL(context_.server_factory_context_.init_manager_, add(_))
        .WillByDefault(Invoke([this](const Init::Target& target) {
          init_target_handles_.push_back(target.createHandle("test"));
        }));

    ON_CALL(context_.server_factory_context_.init_manager_, initialize(_))
        .WillByDefault(Invoke([this](const Init::Watcher& watcher) {
          while (!init_target_handles_.empty()) {
            init_target_handles_.back()->initialize(watcher);
            init_target_handles_.pop_back();
          }
        }));
  }

protected:
  void expectWritesAndLog(AccessLog::InstanceSharedPtr log, int expect_write_times,
                          int log_call_times) {
    EXPECT_CALL(*file_, write(_)).Times(expect_write_times);
    for (int i = 0; i < log_call_times; ++i) {
      log->log({&request_headers_, &response_headers_, &response_trailers_}, stream_info_);
    }
  }

  const std::string default_access_log_ = R"EOF(
name: accesslog
filter:
  extension_filter:
    name: local_ratelimit_extension_filter
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.process_ratelimit.v3.ProcessRateLimitFilter
      dynamic_config:
        resource_name: "token_bucket_name"
        config_source:
          ads: {}
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
  path: /dev/null
  )EOF";

  const envoy::type::v3::TokenBucket token_bucket_resource_ =
      TestUtility::parseYaml<envoy::type::v3::TokenBucket>(R"EOF(
max_tokens: 1
tokens_per_fill: 1
fill_interval:
  seconds: 1
)EOF");

  NiceMock<MockTimeSystem> time_source_;
  Http::TestRequestHeaderMapImpl request_headers_{{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  TestStreamInfo stream_info_;
  std::shared_ptr<AccessLog::MockAccessLogFile> file_;
  StringViewSaver output_;

  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Envoy::AccessLog::MockAccessLogManager> log_manager_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Envoy::Event::SimulatedTimeSystem* time_system_;

  absl::flat_hash_map<std::string, Config::MockSubscription*> subscriptions_;
  absl::flat_hash_map<std::string, Config::SubscriptionCallbacks*> callbackss_;
  std::vector<Init::TargetHandlePtr> init_target_handles_;
  NiceMock<Init::ExpectableWatcherImpl> init_watcher_;
};

TEST_F(AccessLogImplTestWithRateLimitFilter, InvalidConfigWithEmptyDynamicConfig) {
  const std::string invalid_access_log = R"EOF(
name: accesslog
filter:
  extension_filter:
    name: local_ratelimit_extension_filter
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.filters.process_ratelimit.v3.ProcessRateLimitFilter
      dynamic_config:
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
  path: /dev/null
  )EOF";
  EXPECT_THROW(AccessLog::AccessLogFactory::fromProto(parseAccessLogFromV3Yaml(invalid_access_log),
                                                      context_),
               EnvoyException);
}

TEST_F(AccessLogImplTestWithRateLimitFilter, FilterDestructedBeforeCallback) {
  AccessLog::InstanceSharedPtr log1 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  // log2 is to hold the provider singleton alive.
  AccessLog::InstanceSharedPtr log2 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  context_.server_factory_context_.init_manager_.initialize(init_watcher_);
  ASSERT_EQ(subscriptions_.size(), 1);
  ASSERT_EQ(callbackss_.size(), 1);

  // Destruct the log object, which destructs the filter.
  log1.reset();

  // Now, simulate the config update arriving. The lambda captured in
  // getRateLimiter should handle the filter being gone.
  EXPECT_CALL(init_watcher_, ready());
  const auto decoded_resources = TestUtility::decodeResources<envoy::type::v3::TokenBucket>(
      {{"token_bucket_name", token_bucket_resource_}});
  EXPECT_TRUE(callbackss_["token_bucket_name"]->onConfigUpdate(decoded_resources.refvec_, "").ok());

  // No crash should occur. The main thing we are testing is that the callback
  // doesn't try to access any members of the destructed filter.
}

TEST_F(AccessLogImplTestWithRateLimitFilter, HappyPath) {
  AccessLog::InstanceSharedPtr log = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  context_.server_factory_context_.init_manager_.initialize(init_watcher_);
  ASSERT_EQ(subscriptions_.size(), 1);
  ASSERT_EQ(callbackss_.size(), 1);

  EXPECT_CALL(init_watcher_, ready());
  const auto decoded_resources = TestUtility::decodeResources<envoy::type::v3::TokenBucket>(
      {{"token_bucket_name", token_bucket_resource_}});
  EXPECT_TRUE(callbackss_["token_bucket_name"]->onConfigUpdate(decoded_resources.refvec_, "").ok());

  // First log is written, second is rate limited.
  expectWritesAndLog(log, /*expect_write_times=*/1, /*log_call_times=*/2);
  EXPECT_EQ(context_.scope().counterFromString("access_log.process_ratelimit.allowed").value(), 1);

  EXPECT_EQ(context_.scope().counterFromString("access_log.process_ratelimit.denied").value(), 1);

  time_system_->setMonotonicTime(MonotonicTime(std::chrono::seconds(1)));
  // Third log is written, fourth is rate limited.
  expectWritesAndLog(log, /*expect_write_times=*/1, /*log_call_times=*/2);
  EXPECT_EQ(context_.scope().counterFromString("access_log.process_ratelimit.allowed").value(), 2);

  EXPECT_EQ(context_.scope().counterFromString("access_log.process_ratelimit.denied").value(), 2);
}

TEST_F(AccessLogImplTestWithRateLimitFilter, SharedTokenBucketInitTogether) {
  AccessLog::InstanceSharedPtr log1 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  AccessLog::InstanceSharedPtr log2 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  ASSERT_EQ(subscriptions_.size(), 1);
  ASSERT_EQ(callbackss_.size(), 1);

  const auto decoded_resources = TestUtility::decodeResources<envoy::type::v3::TokenBucket>(
      {{"token_bucket_name", token_bucket_resource_}});
  EXPECT_TRUE(callbackss_["token_bucket_name"]->onConfigUpdate(decoded_resources.refvec_, "").ok());

  expectWritesAndLog(log1, /*expect_write_times=*/1, /*log_call_times=*/1);
  expectWritesAndLog(log2, /*expect_write_times=*/0, /*log_call_times=*/1);

  time_system_->setMonotonicTime(MonotonicTime(std::chrono::seconds(1)));
  expectWritesAndLog(log2, /*expect_write_times=*/1, /*log_call_times=*/1);
  expectWritesAndLog(log1, /*expect_write_times=*/0, /*log_call_times=*/1);
}

TEST_F(AccessLogImplTestWithRateLimitFilter, SharedTokenBucketInitSeparately) {
  AccessLog::InstanceSharedPtr log1 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  context_.server_factory_context_.init_manager_.initialize(init_watcher_);
  ASSERT_EQ(subscriptions_.size(), 1);
  ASSERT_EQ(callbackss_.size(), 1);

  EXPECT_CALL(init_watcher_, ready());
  const auto decoded_resources = TestUtility::decodeResources<envoy::type::v3::TokenBucket>(
      {{"token_bucket_name", token_bucket_resource_}});
  EXPECT_TRUE(callbackss_["token_bucket_name"]->onConfigUpdate(decoded_resources.refvec_, "").ok());
  expectWritesAndLog(log1, /*expect_write_times=*/1, /*log_call_times=*/1);

  // Init the second log with the same token bucket.
  AccessLog::InstanceSharedPtr log2 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  context_.server_factory_context_.init_manager_.initialize(init_watcher_);
  expectWritesAndLog(log2, /*expect_write_times=*/0, /*log_call_times=*/1);
  time_system_->setMonotonicTime(MonotonicTime(std::chrono::seconds(1)));
  expectWritesAndLog(log2, /*expect_write_times=*/1, /*log_call_times=*/1);
  expectWritesAndLog(log1, /*expect_write_times=*/0, /*log_call_times=*/1);
}

TEST_F(AccessLogImplTestWithRateLimitFilter, TokenBucketUpdatedUnderSameResourceName) {
  AccessLog::InstanceSharedPtr log1 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  context_.server_factory_context_.init_manager_.initialize(init_watcher_);
  ASSERT_EQ(subscriptions_.size(), 1);
  ASSERT_EQ(callbackss_.size(), 1);

  EXPECT_CALL(init_watcher_, ready());
  const auto decoded_resources = TestUtility::decodeResources<envoy::type::v3::TokenBucket>(
      {{"token_bucket_name", token_bucket_resource_}});
  EXPECT_TRUE(callbackss_["token_bucket_name"]->onConfigUpdate(decoded_resources.refvec_, "").ok());
  expectWritesAndLog(log1, /*expect_write_times=*/1, /*log_call_times=*/2);

  const auto decoded_resources_2 = TestUtility::decodeResources<envoy::type::v3::TokenBucket>(
      {{"token_bucket_name", TestUtility::parseYaml<envoy::type::v3::TokenBucket>(R"EOF(
max_tokens: 2
tokens_per_fill: 2
fill_interval:
  seconds: 1
)EOF")}});
  EXPECT_TRUE(
      callbackss_["token_bucket_name"]->onConfigUpdate(decoded_resources_2.refvec_, "").ok());
  // Init the second log with the same token bucket.
  AccessLog::InstanceSharedPtr log2 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  context_.server_factory_context_.init_manager_.initialize(init_watcher_);
  // The new token bucket allows 2 writes per second. We call log 3 times.
  expectWritesAndLog(log2, /*expect_write_times=*/2, /*log_call_times=*/3);
}

TEST_F(AccessLogImplTestWithRateLimitFilter, RemoveAndAddResource) {
  AccessLog::InstanceSharedPtr log1 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  context_.server_factory_context_.init_manager_.initialize(init_watcher_);
  ASSERT_EQ(subscriptions_.size(), 1);
  ASSERT_EQ(callbackss_.size(), 1);

  // 1. Add the resource
  EXPECT_CALL(init_watcher_, ready());
  auto decoded_resources = TestUtility::decodeResources<envoy::type::v3::TokenBucket>(
      {{"token_bucket_name", token_bucket_resource_}});
  EXPECT_TRUE(callbackss_["token_bucket_name"]->onConfigUpdate(decoded_resources.refvec_, "").ok());
  expectWritesAndLog(log1, /*expect_write_times=*/1, /*log_call_times=*/2);

  time_system_->setMonotonicTime(MonotonicTime(std::chrono::seconds(1)));

  // 2. Remove the token bucket.
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("token_bucket_name");
  EXPECT_TRUE(callbackss_["token_bucket_name"]->onConfigUpdate({}, removed_resources, "").ok());
  // The rate limiter should always deny.
  expectWritesAndLog(log1, /*expect_write_times=*/0, /*log_call_times=*/1);

  // 3. Add the resource back.
  auto new_token_bucket = TestUtility::parseYaml<envoy::type::v3::TokenBucket>(R"EOF(
max_tokens: 3
tokens_per_fill: 3
fill_interval:
  seconds: 3
)EOF");
  decoded_resources = TestUtility::decodeResources<envoy::type::v3::TokenBucket>(
      {{"token_bucket_name", new_token_bucket}});
  EXPECT_TRUE(
      callbackss_["token_bucket_name"]->onConfigUpdate(decoded_resources.refvec_, {}, "").ok());
  // The rate limiter should be working with the new config.
  // time_system_->setMonotonicTime(MonotonicTime(std::chrono::seconds(4)));
  expectWritesAndLog(log1, /*expect_write_times=*/3, /*log_call_times=*/4);

  // A new log instance should also pick up the re-added config.
  AccessLog::InstanceSharedPtr log2 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  context_.server_factory_context_.init_manager_.initialize(init_watcher_);
  // It shares the same token bucket, so it's rate limited.
  expectWritesAndLog(log2, /*expect_write_times=*/0, /*log_call_times=*/1);

  time_system_->setMonotonicTime(MonotonicTime(std::chrono::seconds(4)));
  expectWritesAndLog(log2, /*expect_write_times=*/3, /*log_call_times=*/4);
}

TEST_F(AccessLogImplTestWithRateLimitFilter,
       RemoveResourceAndGetTokenBucketBeforeNewResourceAdded) {
  AccessLog::InstanceSharedPtr log1 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  context_.server_factory_context_.init_manager_.initialize(init_watcher_);
  ASSERT_EQ(subscriptions_.size(), 1);
  ASSERT_EQ(callbackss_.size(), 1);

  // 1. Add the resource
  EXPECT_CALL(init_watcher_, ready());
  auto decoded_resources = TestUtility::decodeResources<envoy::type::v3::TokenBucket>(
      {{"token_bucket_name", token_bucket_resource_}});
  EXPECT_TRUE(callbackss_["token_bucket_name"]->onConfigUpdate(decoded_resources.refvec_, "").ok());
  expectWritesAndLog(log1, /*expect_write_times=*/1, /*log_call_times=*/2);

  time_system_->setMonotonicTime(MonotonicTime(std::chrono::seconds(1)));

  // 2. Remove the token bucket.
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("token_bucket_name");
  EXPECT_TRUE(callbackss_["token_bucket_name"]->onConfigUpdate({}, removed_resources, "").ok());
  // The rate limiter should always deny.
  expectWritesAndLog(log1, /*expect_write_times=*/0, /*log_call_times=*/1);

  // A new log instance should also pick up the re-added config.
  EXPECT_CALL(init_watcher_, ready()).Times(0);
  AccessLog::InstanceSharedPtr log2 = AccessLog::AccessLogFactory::fromProto(
      parseAccessLogFromV3Yaml(default_access_log_), context_);
  context_.server_factory_context_.init_manager_.initialize(init_watcher_);

  // 3. Add the resource back.
  EXPECT_CALL(init_watcher_, ready());
  auto new_token_bucket = TestUtility::parseYaml<envoy::type::v3::TokenBucket>(R"EOF(
max_tokens: 3
tokens_per_fill: 3
fill_interval:
  seconds: 3
)EOF");

  decoded_resources = TestUtility::decodeResources<envoy::type::v3::TokenBucket>(
      {{"token_bucket_name", new_token_bucket}});
  EXPECT_TRUE(
      callbackss_["token_bucket_name"]->onConfigUpdate(decoded_resources.refvec_, {}, "").ok());
  // The rate limiter should be working with the new config.
  expectWritesAndLog(log1, /*expect_write_times=*/3, /*log_call_times=*/4);
  // It shares the same token bucket, so it's rate limited.
  expectWritesAndLog(log2, /*expect_write_times=*/0, /*log_call_times=*/1);

  time_system_->setMonotonicTime(MonotonicTime(std::chrono::seconds(4)));
  expectWritesAndLog(log2, /*expect_write_times=*/3, /*log_call_times=*/4);
}

} // namespace
} // namespace ProcessRateLimit
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
