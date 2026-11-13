#include <array>

#include "envoy/admin/v3/config_dump.pb.h"

#include "source/common/common/matchers.h"
#include "source/extensions/bootstrap/dynamic_modules/extension_config_subscription.h"
#include "source/server/admin/config_tracker_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/server/server_factory_context.h"
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
  std::unique_ptr<ExtensionConfigSubscription>
  create(absl::string_view type_url = "type.googleapis.com/test.Config") {
    auto transport = std::make_unique<testing::StrictMock<Config::MockSubscription>>();
    EXPECT_CALL(*transport, start(testing::ElementsAre("test-config")));
    EXPECT_CALL(context_.xds_manager_,
                subscribeToSingletonResource("test-config", _, _, _,
                                             A<Config::SubscriptionCallbacks&>(), _, _))
        .WillOnce([&](auto, auto, auto, auto&, auto&, auto, auto) -> Config::SubscriptionPtr {
          return std::move(transport);
        });
    ON_CALL(context_, admin()).WillByDefault(Return(OptRef<Server::Admin>{context_.admin_}));
    ON_CALL(context_.admin_, getConfigTracker()).WillByDefault(ReturnRef(tracker_));
    envoy::config::core::v3::ConfigSource source;
    source.mutable_ads();
    source.set_resource_api_version(envoy::config::core::v3::V3);
    auto result = ExtensionConfigSubscription::create(
        "test-config", type_url, source, "ecds_bootstrap.test", context_,
        [this](std::optional<absl::string_view> config) {
          updates_.push_back(config.has_value() ? std::make_optional(std::string(*config))
                                                : std::nullopt);
          return accept_;
        });
    EXPECT_OK(result);
    auto subscription = std::move(result).value();
    subscription->start();
    return subscription;
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

  std::string dump() {
    auto config =
        tracker_.getCallbacksMap().at("ecds_bootstrap.test")(Matchers::UniversalStringMatcher());
    return MessageUtil::getJsonStringFromMessageOrError(*config);
  }

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  Server::ConfigTrackerImpl tracker_;
  bool accept_ = true;
  std::vector<std::optional<std::string>> updates_;
};

TEST_F(ExtensionConfigSubscriptionTest, AppliesTypedStructAndExposesAcceptedConfig) {
  auto subscription = create();
  EXPECT_OK(update(*subscription, config(true)));
  ASSERT_EQ(updates_.size(), 1);
  EXPECT_THAT(updates_[0].value(), testing::HasSubstr("true"));
  EXPECT_THAT(dump(), testing::HasSubstr("v1"));
  EXPECT_THAT(dump(), testing::HasSubstr("test.Config"));
  EXPECT_THAT(dump(), testing::HasSubstr("true"));
  subscription.reset();
  EXPECT_TRUE(tracker_.getCallbacksMap().empty());
}

TEST_F(ExtensionConfigSubscriptionTest, AppliesConfigWithoutAdmin) {
  EXPECT_CALL(context_, admin()).WillOnce(Return(OptRef<Server::Admin>{}));
  auto subscription = create();
  EXPECT_OK(update(*subscription, config(true)));
  ASSERT_EQ(updates_.size(), 1);
  EXPECT_THAT(updates_[0].value(), testing::HasSubstr("true"));
  EXPECT_TRUE(tracker_.getCallbacksMap().empty());
}

TEST_F(ExtensionConfigSubscriptionTest, DuplicateConfigDumpEntryPreservesExistingSubscription) {
  auto subscription = create();
  EXPECT_OK(update(*subscription, config(true)));
  const auto accepted = dump();
  EXPECT_CALL(context_.xds_manager_,
              subscribeToSingletonResource("test-config", _, _, _,
                                           A<Config::SubscriptionCallbacks&>(), _, _))
      .WillOnce([](auto, auto, auto, auto&, auto&, auto, auto) -> Config::SubscriptionPtr {
        return std::make_unique<testing::StrictMock<Config::MockSubscription>>();
      });
  envoy::config::core::v3::ConfigSource source;
  auto duplicate = ExtensionConfigSubscription::create(
      "test-config", "test.Config", source, "ecds_bootstrap.test", context_,
      [](std::optional<absl::string_view>) { return true; });
  EXPECT_THAT(duplicate, StatusHelpers::HasStatus(absl::StatusCode::kAlreadyExists,
                                                  testing::HasSubstr("ecds_bootstrap.test")));
  EXPECT_EQ(tracker_.getCallbacksMap().size(), 1);
  EXPECT_EQ(dump(), accepted);
  EXPECT_OK(update(*subscription, config(false), "v2"));
  EXPECT_THAT(dump(), testing::HasSubstr("v2"));
}

TEST_F(ExtensionConfigSubscriptionTest,
       RejectsInvalidNameTypeAndMissingConfigBeforeModuleCallback) {
  auto subscription = create();
  auto invalid = config(true);
  invalid.set_name("other-config");
  EXPECT_FALSE(update(*subscription, invalid).ok());
  invalid = config(true);
  MessageUtil::packFrom(*invalid.mutable_typed_config(), Protobuf::StringValue());
  EXPECT_FALSE(update(*subscription, invalid).ok());
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
  EXPECT_EQ(updates_[0].value(), "{}");
}

TEST_F(ExtensionConfigSubscriptionTest, AppliesStructPayload) {
  auto subscription = create("type.googleapis.com/google.protobuf.Struct");
  Protobuf::Struct payload;
  (*payload.mutable_fields())["enabled"].set_bool_value(true);
  auto resource = config(true);
  MessageUtil::packFrom(*resource.mutable_typed_config(), payload);
  EXPECT_OK(update(*subscription, resource));
  ASSERT_EQ(updates_.size(), 1);
  EXPECT_EQ(updates_[0].value(), R"({"enabled":true})");
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
    EXPECT_EQ(updates_.back(), std::make_optional(value));
  }
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
  EXPECT_EQ(updates_[0].value(), std::string("\x08\x00", 2));
  EXPECT_EQ(updates_[1].value(), "");
}

TEST_F(ExtensionConfigSubscriptionTest, RejectsEmptyNamesAndSubscriptionFailure) {
  envoy::config::core::v3::ConfigSource source;
  auto callback = [](std::optional<absl::string_view>) { return true; };
  EXPECT_FALSE(
      ExtensionConfigSubscription::create("", "test.Config", source, "test", context_, callback)
          .ok());
  EXPECT_FALSE(
      ExtensionConfigSubscription::create("test", "", source, "test", context_, callback).ok());
  EXPECT_CALL(
      context_.xds_manager_,
      subscribeToSingletonResource("test", _, _, _, A<Config::SubscriptionCallbacks&>(), _, _))
      .WillOnce(Return(absl::UnavailableError("subscription unavailable")));
  EXPECT_FALSE(
      ExtensionConfigSubscription::create("test", "test.Config", source, "test", context_, callback)
          .ok());
}

TEST_F(ExtensionConfigSubscriptionTest, RejectionAndConnectionFailureRetainAcceptedConfig) {
  auto subscription = create();
  EXPECT_OK(update(*subscription, config(true)));
  const auto accepted = dump();
  accept_ = false;
  EXPECT_FALSE(update(*subscription, config(false), "v2").ok());
  EXPECT_EQ(dump(), accepted);
  subscription->onConfigUpdateFailed(Config::ConfigUpdateFailureReason::ConnectionFailure, nullptr);
  EXPECT_EQ(dump(), accepted);
  EXPECT_EQ(updates_.size(), 2);
  EXPECT_FALSE(subscription->onConfigUpdate({}, "removed").ok());
  EXPECT_EQ(dump(), accepted);
}

TEST_F(ExtensionConfigSubscriptionTest, DeltaUpdatesAndRemovalPreservePresence) {
  auto subscription = create();
  auto added = TestUtility::decodeResources({config(true)});
  Protobuf::RepeatedPtrField<std::string> removed;
  EXPECT_OK(subscription->onConfigUpdate(added.refvec_, removed, "v1"));
  EXPECT_OK(subscription->onConfigUpdate({}, removed, "empty-delta"));
  EXPECT_EQ(updates_.size(), 1);
  removed.Add("other-config");
  EXPECT_FALSE(subscription->onConfigUpdate({}, removed, "v2").ok());
  removed.Mutable(0)->assign("test-config");
  EXPECT_FALSE(subscription->onConfigUpdate(added.refvec_, removed, "v2").ok());
  EXPECT_OK(subscription->onConfigUpdate({}, removed, "v2"));
  ASSERT_EQ(updates_.size(), 2);
  EXPECT_FALSE(updates_.back().has_value());
  EXPECT_THAT(dump(), testing::Not(testing::HasSubstr("test-config")));
  EXPECT_OK(update(*subscription, config(false)));
  EXPECT_OK(subscription->onConfigUpdate({}, "sotw-removal"));
  EXPECT_FALSE(updates_.back().has_value());
}

} // namespace
} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
