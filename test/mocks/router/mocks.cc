#include "mocks.h"

#include <chrono>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Router {

MockDirectResponseEntry::MockDirectResponseEntry() = default;
MockDirectResponseEntry::~MockDirectResponseEntry() = default;

TestRetryPolicy::TestRetryPolicy() { num_retries_ = 1; }

TestRetryPolicy::~TestRetryPolicy() = default;

MockInternalRedirectPolicy::MockInternalRedirectPolicy() {
  ON_CALL(*this, enabled()).WillByDefault(Return(false));
}

MockRetryState::MockRetryState() = default;

void MockRetryState::expectHeadersRetry() {
  EXPECT_CALL(*this, shouldRetryHeaders(_, _, _))
      .WillOnce(Invoke([this](const Http::ResponseHeaderMap&, const Http::RequestHeaderMap&,
                              DoRetryHeaderCallback callback) {
        callback_ = [callback]() { callback(false); };
        return RetryStatus::Yes;
      }));
}

void MockRetryState::expectHedgedPerTryTimeoutRetry() {
  EXPECT_CALL(*this, shouldHedgeRetryPerTryTimeout(_))
      .WillOnce(DoAll(SaveArg<0>(&callback_), Return(RetryStatus::Yes)));
}

void MockRetryState::expectResetRetry() {
  EXPECT_CALL(*this, shouldRetryReset(_, _, _))
      .WillOnce(Invoke([this](const Http::StreamResetReason, RetryState::Http3Used,
                              DoRetryResetCallback callback) {
        callback_ = [callback]() { callback(false); };
        return RetryStatus::Yes;
      }));
}

MockRetryState::~MockRetryState() = default;

MockRateLimitPolicyEntry::MockRateLimitPolicyEntry() {
  ON_CALL(*this, disableKey()).WillByDefault(ReturnRef(disable_key_));
}

MockRateLimitPolicyEntry::~MockRateLimitPolicyEntry() = default;

MockRateLimitPolicy::MockRateLimitPolicy() {
  ON_CALL(*this, getApplicableRateLimit(_)).WillByDefault(ReturnRef(rate_limit_policy_entry_));
  ON_CALL(*this, empty()).WillByDefault(Return(true));
}

MockRateLimitPolicy::~MockRateLimitPolicy() = default;

MockShadowWriter::MockShadowWriter() = default;
MockShadowWriter::~MockShadowWriter() = default;

MockVirtualHost::MockVirtualHost() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, rateLimitPolicy()).WillByDefault(ReturnRef(rate_limit_policy_));
  ON_CALL(*this, metadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(*this, typedMetadata()).WillByDefault(ReturnRef(typed_metadata_));
}

MockVirtualHost::~MockVirtualHost() = default;

MockHashPolicy::MockHashPolicy() = default;
MockHashPolicy::~MockHashPolicy() = default;

MockMetadataMatchCriteria::MockMetadataMatchCriteria() = default;
MockMetadataMatchCriteria::~MockMetadataMatchCriteria() = default;

MockTlsContextMatchCriteria::MockTlsContextMatchCriteria() = default;
MockTlsContextMatchCriteria::~MockTlsContextMatchCriteria() = default;

MockPathMatchCriterion::MockPathMatchCriterion() {
  ON_CALL(*this, matchType()).WillByDefault(ReturnPointee(&type_));
  ON_CALL(*this, matcher()).WillByDefault(ReturnPointee(&matcher_));
}

MockPathMatchCriterion::~MockPathMatchCriterion() = default;

MockRouteEntry::MockRouteEntry() {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
  ON_CALL(*this, opaqueConfig()).WillByDefault(ReturnRef(opaque_config_));
  ON_CALL(*this, rateLimitPolicy()).WillByDefault(ReturnRef(rate_limit_policy_));
  ON_CALL(*this, retryPolicy()).WillByDefault(ReturnRef(retry_policy_));
  ON_CALL(*this, internalRedirectPolicy()).WillByDefault(ReturnRef(internal_redirect_policy_));
  ON_CALL(*this, retryShadowBufferLimit())
      .WillByDefault(Return(std::numeric_limits<uint32_t>::max()));
  ON_CALL(*this, shadowPolicies()).WillByDefault(ReturnRef(shadow_policies_));
  ON_CALL(*this, timeout()).WillByDefault(Return(std::chrono::milliseconds(10)));
  ON_CALL(*this, virtualCluster(_)).WillByDefault(Return(&virtual_cluster_));
  ON_CALL(*this, virtualHost()).WillByDefault(ReturnRef(virtual_host_));
  ON_CALL(*this, includeVirtualHostRateLimits()).WillByDefault(Return(true));
  ON_CALL(*this, pathMatchCriterion()).WillByDefault(ReturnRef(path_match_criterion_));
  ON_CALL(*this, upgradeMap()).WillByDefault(ReturnRef(upgrade_map_));
  ON_CALL(*this, hedgePolicy()).WillByDefault(ReturnRef(hedge_policy_));
  ON_CALL(*this, connectConfig()).WillByDefault(Invoke([this]() {
    return connect_config_.has_value() ? makeOptRef(connect_config_.value()) : absl::nullopt;
  }));
  ON_CALL(*this, earlyDataPolicy()).WillByDefault(ReturnRef(early_data_policy_));
  path_matcher_ = std::make_shared<testing::NiceMock<MockPathMatcher>>();
  ON_CALL(*this, pathMatcher()).WillByDefault(ReturnRef(path_matcher_));
  path_rewriter_ = std::make_shared<testing::NiceMock<MockPathRewriter>>();
  ON_CALL(*this, pathRewriter()).WillByDefault(ReturnRef(path_rewriter_));
  ON_CALL(*this, routeStatsContext()).WillByDefault(Return(RouteStatsContextOptRef()));
}

MockRouteEntry::~MockRouteEntry() = default;

MockConfig::MockConfig() : route_(new NiceMock<MockRoute>()) {
  ON_CALL(*this, route(_, _, _)).WillByDefault(Return(route_));
  ON_CALL(*this, route(_, _, _, _)).WillByDefault(Return(route_));
  ON_CALL(*this, internalOnlyHeaders()).WillByDefault(ReturnRef(internal_only_headers_));
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, usesVhds()).WillByDefault(Return(false));
  ON_CALL(*this, metadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(*this, typedMetadata()).WillByDefault(ReturnRef(typed_metadata_));
}

MockConfig::~MockConfig() = default;

MockDecorator::MockDecorator() {
  ON_CALL(*this, getOperation()).WillByDefault(ReturnRef(operation_));
  ON_CALL(*this, propagate()).WillByDefault(Return(true));
}
MockDecorator::~MockDecorator() = default;

MockRouteTracing::MockRouteTracing() = default;
MockRouteTracing::~MockRouteTracing() = default;

MockRoute::MockRoute() {
  ON_CALL(*this, routeEntry()).WillByDefault(Return(&route_entry_));
  ON_CALL(*this, decorator()).WillByDefault(Return(&decorator_));
  ON_CALL(*this, tracingConfig()).WillByDefault(Return(nullptr));
  ON_CALL(*this, metadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(*this, typedMetadata()).WillByDefault(ReturnRef(typed_metadata_));
  ON_CALL(*this, routeName()).WillByDefault(ReturnRef(route_name_));
}
MockRoute::~MockRoute() = default;

MockRouteConfigProvider::MockRouteConfigProvider() {
  ON_CALL(*this, config()).WillByDefault(Return(route_config_));
  ON_CALL(*this, configCast()).WillByDefault(Return(route_config_));
}
MockRouteConfigProvider::~MockRouteConfigProvider() = default;

MockRouteConfigProviderManager::MockRouteConfigProviderManager() = default;
MockRouteConfigProviderManager::~MockRouteConfigProviderManager() = default;

MockScopedConfig::MockScopedConfig() {
  ON_CALL(*this, getRouteConfig(_)).WillByDefault(Return(route_config_));
}
MockScopedConfig::~MockScopedConfig() = default;

MockScopedRouteConfigProvider::MockScopedRouteConfigProvider()
    : config_(std::make_shared<MockScopedConfig>()) {
  ON_CALL(*this, getConfig()).WillByDefault(Return(config_));
  ON_CALL(*this, apiType()).WillByDefault(Return(ApiType::Delta));
}
MockScopedRouteConfigProvider::~MockScopedRouteConfigProvider() = default;

MockScopeKeyBuilder::MockScopeKeyBuilder() {
  ON_CALL(*this, computeScopeKey(_))
      .WillByDefault(Invoke([](const Http::HeaderMap&) -> ScopeKeyPtr { return nullptr; }));
}
MockScopeKeyBuilder::~MockScopeKeyBuilder() = default;

MockGenericConnectionPoolCallbacks::MockGenericConnectionPoolCallbacks() {
  ON_CALL(*this, upstreamToDownstream()).WillByDefault(ReturnRef(upstream_to_downstream_));
}

MockClusterSpecifierPlugin::MockClusterSpecifierPlugin() {
  ON_CALL(*this, route(_, _)).WillByDefault(Return(nullptr));
}

MockClusterSpecifierPluginFactoryConfig::MockClusterSpecifierPluginFactoryConfig() {
  ON_CALL(*this, createClusterSpecifierPlugin(_, _)).WillByDefault(Return(nullptr));
}

} // namespace Router
} // namespace Envoy
