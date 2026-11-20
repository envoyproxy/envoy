#include <array>

#include "envoy/admin/v3/config_dump.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/config/ecds_subscription_registry.h"
#include "source/common/config/utility.h"
#include "source/extensions/bootstrap/dynamic_modules/extension_config_subscription.h"
#include "source/server/admin/config_tracker_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "xds/type/v3/typed_struct.pb.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {
namespace {

using testing::_;
using testing::A;
using testing::Return;
using testing::ReturnRef;

class ExtensionConfigSubscriptionTest : public testing::Test {
protected:
  ExtensionConfigSubscriptionTest() {
    ON_CALL(context_, admin()).WillByDefault(Return(OptRef<Server::Admin>{context_.admin_}));
    ON_CALL(context_.admin_, getConfigTracker()).WillByDefault(ReturnRef(tracker_));
  }

  static envoy::config::core::v3::ExtensionConfigSource
  source(absl::string_view type_url = "type.googleapis.com/test.Config", bool ads = false) {
    envoy::config::core::v3::ExtensionConfigSource source;
    if (ads) {
      source.mutable_config_source()->mutable_ads();
    } else {
      source.mutable_config_source()->mutable_path_config_source()->set_path("unused.yaml");
    }
    source.mutable_config_source()->set_resource_api_version(envoy::config::core::v3::V3);
    source.add_type_urls(type_url);
    return source;
  }

  std::unique_ptr<ExtensionConfigSubscription>
  create(absl::string_view type_url = "type.googleapis.com/test.Config",
         absl::string_view name = "test-config", bool ads = false) {
    auto transport = std::make_unique<testing::StrictMock<Config::MockSubscription>>();
    EXPECT_CALL(*transport, start(testing::ElementsAre(name)));
    EXPECT_CALL(
        context_.xds_manager_,
        subscribeToSingletonResource(name, _, _, _, A<Config::SubscriptionCallbacks&>(), _, _))
        .WillOnce([&](auto, auto, auto, Stats::Scope& scope, auto&, auto,
                      auto) -> Config::SubscriptionPtr {
          auto stats = Config::Utility::generateStats(scope);
          EXPECT_EQ(
              stats.update_success_.name(),
              absl::StrCat("extension_config_discovery.bootstrap.", name, ".test.update_success"));
          return std::move(transport);
        });
    auto result = ExtensionConfigSubscription::create(name, source(type_url, ads), "test", context_,
                                                      callback());
    EXPECT_OK(result);
    auto subscription = std::move(result).value();
    subscription->start();
    return subscription;
  }

  ExtensionConfigSubscription::UpdateCallback callback() {
    return [this](absl::string_view name, absl::string_view version, absl::string_view type_url,
                  std::optional<absl::string_view> config) {
      updates_.push_back(
          {std::string(name), std::string(version), std::string(type_url),
           config.has_value() ? std::make_optional(std::string(*config)) : std::nullopt});
      return accept_;
    };
  }

  envoy::config::core::v3::TypedExtensionConfig config(bool enabled) {
    xds::type::v3::TypedStruct typed;
    typed.set_type_url("type.googleapis.com/test.Config");
    (*typed.mutable_value()->mutable_fields())["enabled"].set_bool_value(enabled);
    envoy::config::core::v3::TypedExtensionConfig result;
    result.set_name("test-config");
    MessageUtil::packFrom(*result.mutable_typed_config(), typed);
    return result;
  }

  absl::Status update(ExtensionConfigSubscription& subscription,
                      const envoy::config::core::v3::TypedExtensionConfig& config,
                      const std::string& version = "v1") {
    auto resources = TestUtility::decodeResources({config});
    return subscription.onConfigUpdate(resources.refvec_, version);
  }

  envoy::admin::v3::EcdsConfigDump
  dump(const Matchers::StringMatcher& matcher = Matchers::UniversalStringMatcher()) {
    auto message = tracker_.getCallbacksMap().at("ecds_modules")(matcher);
    return Protobuf::DynamicCastMessage<envoy::admin::v3::EcdsConfigDump>(*message);
  }

  struct Update {
    std::string name;
    std::string version;
    std::string type_url;
    std::optional<std::string> config;
  };
  Event::SimulatedTimeSystem time_system_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Server::ConfigTrackerImpl tracker_;
  bool accept_ = true;
  std::vector<Update> updates_;
};

TEST_F(ExtensionConfigSubscriptionTest, AppliesTypedStructAndExposesAcceptedConfig) {
  auto subscription = create();
  EXPECT_OK(update(*subscription, config(true)));
  ASSERT_EQ(updates_.size(), 1);
  EXPECT_EQ(updates_[0].name, "test-config");
  EXPECT_EQ(updates_[0].version, "v1");
  EXPECT_EQ(updates_[0].type_url, "type.googleapis.com/test.Config");
  EXPECT_EQ(updates_[0].config.value(), R"({"enabled":true})");
  envoy::admin::v3::EcdsConfigDump expected;
  auto* entry = expected.add_ecds_filters();
  MessageUtil::packFrom(*entry->mutable_ecds_filter(), config(true));
  entry->set_version_info("v1");
  TimestampUtil::systemClockToTimestamp(context_.timeSource().systemTime(),
                                        *entry->mutable_last_updated());
  EXPECT_THAT(dump(), ProtoEq(expected));
  subscription.reset();
  EXPECT_TRUE(tracker_.getCallbacksMap().empty());
}

TEST_F(ExtensionConfigSubscriptionTest, DumpHonorsNameFilter) {
  auto subscription = create();
  EXPECT_OK(update(*subscription, config(true)));
  EXPECT_EQ(
      dump(Matchers::StringMatcherImpl::createExactMatcher("another-config")).ecds_filters_size(),
      0);
}

TEST_F(ExtensionConfigSubscriptionTest, AppliesConfigWithoutAdmin) {
  EXPECT_CALL(context_, admin()).WillOnce(Return(OptRef<Server::Admin>{}));
  auto subscription = create();
  EXPECT_OK(update(*subscription, config(true)));
  ASSERT_EQ(updates_.size(), 1);
  EXPECT_EQ(updates_[0].config.value(), R"({"enabled":true})");
  EXPECT_TRUE(tracker_.getCallbacksMap().empty());
}

TEST_F(ExtensionConfigSubscriptionTest, SubscriptionsShareOneConfigDump) {
  auto first = create();
  EXPECT_OK(update(*first, config(true)));
  auto second = create("type.googleapis.com/test.Config", "other-config");
  auto other = config(false);
  other.set_name("other-config");
  EXPECT_OK(update(*second, other, "v2"));
  auto pending = create("type.googleapis.com/test.Config", "pending-config");
  ASSERT_EQ(tracker_.getCallbacksMap().size(), 1);
  const auto actual = dump();
  ASSERT_EQ(actual.ecds_filters_size(), 2);
  EXPECT_THAT(actual.ecds_filters(),
              testing::UnorderedElementsAre(
                  testing::Property(
                      &envoy::admin::v3::EcdsConfigDump::EcdsFilterConfig::version_info, "v1"),
                  testing::Property(
                      &envoy::admin::v3::EcdsConfigDump::EcdsFilterConfig::version_info, "v2")));
  second.reset();
  EXPECT_EQ(dump().ecds_filters_size(), 1);
  first.reset();
  EXPECT_EQ(dump().ecds_filters_size(), 0);
  pending.reset();
  EXPECT_TRUE(tracker_.getCallbacksMap().empty());
}

TEST_F(ExtensionConfigSubscriptionTest, RefusesRepeatedAdsNamesAndReleasesReservation) {
  auto first = create("type.googleapis.com/test.Config", "test-config", true);
  EXPECT_OK(update(*first, config(true)));
  EXPECT_CALL(context_.xds_manager_,
              subscribeToSingletonResource(_, _, _, _, A<Config::SubscriptionCallbacks&>(), _, _))
      .Times(0);
  EXPECT_FALSE(ExtensionConfigSubscription::create("test-config", source("test.Config", true),
                                                   "other", context_, callback())
                   .ok());
  auto registry = Config::EcdsSubscriptionRegistry::get(context_);
  EXPECT_FALSE(
      registry->reserve("test-config", Config::EcdsSubscriptionRegistry::Sharing::Shared).ok());
  first.reset();
  EXPECT_OK(registry->reserve("test-config", Config::EcdsSubscriptionRegistry::Sharing::Shared));
}

TEST_F(ExtensionConfigSubscriptionTest, RefusesAnAdsNameAlreadyHeldByFilters) {
  auto registry = Config::EcdsSubscriptionRegistry::get(context_);
  auto first_filter =
      registry->reserve("test-config", Config::EcdsSubscriptionRegistry::Sharing::Shared);
  auto second_filter =
      registry->reserve("test-config", Config::EcdsSubscriptionRegistry::Sharing::Shared);
  ASSERT_OK(first_filter);
  ASSERT_OK(second_filter);
  EXPECT_CALL(context_.xds_manager_,
              subscribeToSingletonResource(_, _, _, _, A<Config::SubscriptionCallbacks&>(), _, _))
      .Times(0);
  EXPECT_FALSE(ExtensionConfigSubscription::create("test-config", source("test.Config", true),
                                                   "test", context_, callback())
                   .ok());
  first_filter->reset();
  EXPECT_FALSE(ExtensionConfigSubscription::create("test-config", source("test.Config", true),
                                                   "test", context_, callback())
                   .ok());
  second_filter->reset();
  EXPECT_OK(registry->reserve("test-config", Config::EcdsSubscriptionRegistry::Sharing::Exclusive));
}

TEST_F(ExtensionConfigSubscriptionTest, RefusesEquivalentXdsTpNamesAndReleasesReservation) {
  const std::string name =
      "xdstp://authority/envoy.config.core.v3.TypedExtensionConfig/test-config?a=1&b=2";
  const std::string reordered =
      "xdstp://authority/envoy.config.core.v3.TypedExtensionConfig/test-config?b=2&a=1";
  EXPECT_CALL(context_.xds_manager_, subscribeToSingletonResource(
                                         name, _, _, _, A<Config::SubscriptionCallbacks&>(), _, _))
      .WillOnce(Return(
          testing::ByMove(std::make_unique<testing::StrictMock<Config::MockSubscription>>())));
  auto subscription =
      ExtensionConfigSubscription::create(name, source(), "test", context_, callback());
  ASSERT_OK(subscription);
  EXPECT_CALL(context_.xds_manager_,
              subscribeToSingletonResource(_, _, _, _, A<Config::SubscriptionCallbacks&>(), _, _))
      .Times(0);
  EXPECT_FALSE(
      ExtensionConfigSubscription::create(reordered, source(), "other", context_, callback()).ok());
  auto registry = Config::EcdsSubscriptionRegistry::get(context_);
  EXPECT_FALSE(
      registry->reserve(reordered, Config::EcdsSubscriptionRegistry::Sharing::Shared).ok());
  subscription->reset();
  EXPECT_OK(registry->reserve(reordered, Config::EcdsSubscriptionRegistry::Sharing::Shared));
}

TEST_F(ExtensionConfigSubscriptionTest,
       RejectsInvalidNameTypeAndMissingConfigBeforeModuleCallback) {
  auto subscription = create();
  auto invalid = config(true);
  invalid.set_name("other-config");
  EXPECT_THAT(
      update(*subscription, invalid).message(),
      testing::AllOf(testing::HasSubstr("test-config"), testing::HasSubstr("other-config")));
  invalid = config(true);
  MessageUtil::packFrom(*invalid.mutable_typed_config(), Protobuf::StringValue());
  EXPECT_THAT(update(*subscription, invalid).message(),
              testing::AllOf(testing::HasSubstr("test-config"),
                             testing::HasSubstr("google.protobuf.StringValue"),
                             testing::HasSubstr("test.Config")));
  invalid.clear_typed_config();
  EXPECT_FALSE(update(*subscription, invalid).ok());
  auto duplicate = TestUtility::decodeResources({config(true), config(false)});
  EXPECT_FALSE(subscription->onConfigUpdate(duplicate.refvec_, "v1").ok());
  EXPECT_TRUE(updates_.empty());
}

TEST_F(ExtensionConfigSubscriptionTest, MissingTypedStructValueIsAnEmptyObject) {
  auto subscription = create();
  xds::type::v3::TypedStruct typed;
  typed.set_type_url("type.googleapis.com/test.Config");
  auto empty = config(true);
  MessageUtil::packFrom(*empty.mutable_typed_config(), typed);
  EXPECT_OK(update(*subscription, empty));
  ASSERT_EQ(updates_.size(), 1);
  EXPECT_EQ(updates_[0].config.value(), "{}");
}

TEST_F(ExtensionConfigSubscriptionTest, AppliesStructPayload) {
  auto subscription = create("type.googleapis.com/google.protobuf.Struct");
  Protobuf::Struct payload;
  (*payload.mutable_fields())["enabled"].set_bool_value(true);
  auto resource = config(true);
  MessageUtil::packFrom(*resource.mutable_typed_config(), payload);
  EXPECT_OK(update(*subscription, resource));
  ASSERT_EQ(updates_.size(), 1);
  EXPECT_EQ(updates_[0].config.value(), R"({"enabled":true})");
}

TEST_F(ExtensionConfigSubscriptionTest, AppliesStringAndBytesWrapperPayloads) {
  const std::string value("hello\0world", 11);
  Protobuf::StringValue string;
  string.set_value(value);
  Protobuf::BytesValue bytes;
  bytes.set_value(value);
  std::array<Protobuf::Any, 2> payloads;
  MessageUtil::packFrom(payloads[0], string);
  MessageUtil::packFrom(payloads[1], bytes);
  for (const auto& payload : payloads) {
    auto subscription = create(payload.type_url());
    auto resource = config(true);
    *resource.mutable_typed_config() = payload;
    const auto previous_updates = updates_.size();
    EXPECT_OK(update(*subscription, resource));
    ASSERT_EQ(updates_.size(), previous_updates + 1);
    EXPECT_EQ(updates_.back().config, std::make_optional(value));
  }
}

TEST_F(ExtensionConfigSubscriptionTest, AcceptsEachPermittedPayloadType) {
  auto config_source = source("type.googleapis.com/test.Config");
  config_source.add_type_urls("type.googleapis.com/google.protobuf.StringValue");
  EXPECT_CALL(context_.xds_manager_,
              subscribeToSingletonResource(_, _, _, _, A<Config::SubscriptionCallbacks&>(), _, _))
      .WillOnce(Return(
          testing::ByMove(std::make_unique<testing::StrictMock<Config::MockSubscription>>())));
  auto subscription = ExtensionConfigSubscription::create("test-config", config_source, "test",
                                                          context_, callback());
  ASSERT_OK(subscription);
  EXPECT_OK(update(**subscription, config(true)));
  auto wrapper = config(true);
  Protobuf::StringValue value;
  value.set_value("enabled");
  MessageUtil::packFrom(*wrapper.mutable_typed_config(), value);
  EXPECT_OK(update(**subscription, wrapper, "v2"));
  ASSERT_EQ(updates_.size(), 2);
  EXPECT_EQ(updates_[0].type_url, "type.googleapis.com/test.Config");
  EXPECT_EQ(updates_[1].type_url, "type.googleapis.com/google.protobuf.StringValue");
  EXPECT_EQ(updates_[1].config.value(), "enabled");
}

TEST_F(ExtensionConfigSubscriptionTest, PreservesBinaryAndEmptyPayloads) {
  auto subscription = create();
  auto resource = config(true);
  resource.mutable_typed_config()->set_type_url("type.googleapis.com/test.Config");
  resource.mutable_typed_config()->set_value(std::string("\x08\x00", 2));
  EXPECT_OK(update(*subscription, resource));
  resource.mutable_typed_config()->clear_value();
  EXPECT_OK(update(*subscription, resource));
  ASSERT_EQ(updates_.size(), 2);
  EXPECT_EQ(updates_[0].config.value(), std::string("\x08\x00", 2));
  EXPECT_EQ(updates_[1].config.value(), "");
}

TEST_F(ExtensionConfigSubscriptionTest, RejectsEmptyAndWildcardNamesAndSubscriptionFailure) {
  for (const auto* name :
       {"", "*", "xdstp://authority/envoy.config.core.v3.TypedExtensionConfig/config/*"}) {
    EXPECT_FALSE(
        ExtensionConfigSubscription::create(name, source(), "test", context_, callback()).ok());
  }
  EXPECT_CALL(context_.xds_manager_,
              subscribeToSingletonResource("test-config", _, _, _,
                                           A<Config::SubscriptionCallbacks&>(), _, _))
      .WillOnce(Return(absl::UnavailableError("subscription unavailable")));
  EXPECT_FALSE(
      ExtensionConfigSubscription::create("test-config", source(), "test", context_, callback())
          .ok());
}

TEST_F(ExtensionConfigSubscriptionTest, RejectsUnsupportedDefaultsBeforeSubscribing) {
  EXPECT_CALL(context_.xds_manager_,
              subscribeToSingletonResource(_, _, _, _, A<Config::SubscriptionCallbacks&>(), _, _))
      .Times(0);
  auto config_source = source();
  config_source.mutable_default_config()->set_type_url("type.googleapis.com/test.Config");
  EXPECT_FALSE(ExtensionConfigSubscription::create("test-config", config_source, "test", context_,
                                                   callback())
                   .ok());
  config_source.clear_default_config();
  config_source.set_apply_default_config_without_warming(true);
  EXPECT_FALSE(ExtensionConfigSubscription::create("test-config", config_source, "test", context_,
                                                   callback())
                   .ok());
}

TEST_F(ExtensionConfigSubscriptionTest, RejectionAndConnectionFailureRetainAcceptedConfig) {
  auto subscription = create();
  EXPECT_OK(update(*subscription, config(true)));
  const auto accepted = dump();
  accept_ = false;
  EXPECT_FALSE(update(*subscription, config(false), "v2").ok());
  EXPECT_THAT(dump(), ProtoEq(accepted));
  for (auto reason : {Config::ConfigUpdateFailureReason::ConnectionFailure,
                      Config::ConfigUpdateFailureReason::FetchTimedout,
                      Config::ConfigUpdateFailureReason::UpdateRejected}) {
    subscription->onConfigUpdateFailed(reason, nullptr);
    EXPECT_THAT(dump(), ProtoEq(accepted));
  }
  EXPECT_EQ(updates_.size(), 2);
}

TEST_F(ExtensionConfigSubscriptionTest, UnchangedResendsAndEmptySotwUpdatesAreNoOps) {
  auto subscription = create();
  EXPECT_OK(update(*subscription, config(true)));
  const auto accepted = dump();
  time_system_.advanceTimeWait(std::chrono::seconds(1));
  EXPECT_OK(update(*subscription, config(true), "v2"));
  EXPECT_OK(subscription->onConfigUpdate({}, "heartbeat"));
  EXPECT_EQ(updates_.size(), 1);
  EXPECT_THAT(dump(), ProtoEq(accepted));
}

TEST_F(ExtensionConfigSubscriptionTest, DeltaUpdatesAndRemovalsPreservePresence) {
  auto subscription = create();
  auto added = TestUtility::decodeResources({config(true)});
  Protobuf::RepeatedPtrField<std::string> removed;
  EXPECT_OK(subscription->onConfigUpdate(added.refvec_, removed, "system-version"));
  ASSERT_EQ(updates_.size(), 1);
  EXPECT_EQ(updates_[0].version, added.refvec_[0].get().version());
  EXPECT_OK(subscription->onConfigUpdate({}, removed, "empty-delta"));
  EXPECT_EQ(updates_.size(), 1);
  removed.Add("other-config");
  EXPECT_OK(subscription->onConfigUpdate({}, removed, "v2"));
  EXPECT_EQ(updates_.size(), 1);
  removed.Add("test-config");
  EXPECT_OK(subscription->onConfigUpdate({}, removed, "v2"));
  ASSERT_EQ(updates_.size(), 2);
  EXPECT_FALSE(updates_.back().config.has_value());
  EXPECT_EQ(updates_.back().name, "test-config");
  EXPECT_TRUE(updates_.back().version.empty());
  EXPECT_EQ(dump().ecds_filters_size(), 0);
  EXPECT_OK(update(*subscription, config(true)));
  EXPECT_EQ(updates_.size(), 3);
}

TEST_F(ExtensionConfigSubscriptionTest, RejectedRemovalsAreLoggedAndDoNotFail) {
  auto subscription = create();
  EXPECT_OK(update(*subscription, config(true)));
  const auto accepted = dump();
  accept_ = false;
  Protobuf::RepeatedPtrField<std::string> removed;
  removed.Add("test-config");
  EXPECT_LOG_CONTAINS("warn", "rejected ECDS removal for test-config",
                      { EXPECT_OK(subscription->onConfigUpdate({}, removed, "v2")); });
  EXPECT_THAT(dump(), ProtoEq(accepted));
}

} // namespace
} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
