#include "mocks.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Router {

MockRedirectEntry::MockRedirectEntry() {}
MockRedirectEntry::~MockRedirectEntry() {}

MockRetryState::MockRetryState() {}

void MockRetryState::expectRetry() {
  EXPECT_CALL(*this, shouldRetry(_, _, _)).WillOnce(DoAll(SaveArg<2>(&callback_), Return(true)));
}

MockRetryState::~MockRetryState() {}

MockRateLimitPolicyEntry::MockRateLimitPolicyEntry() {
  ON_CALL(*this, disableKey()).WillByDefault(ReturnRef(disable_key_));
}

MockRateLimitPolicyEntry::~MockRateLimitPolicyEntry() {}

MockRateLimitPolicy::MockRateLimitPolicy() {
  ON_CALL(*this, getApplicableRateLimit(_)).WillByDefault(ReturnRef(rate_limit_policy_entry_));
}

MockRateLimitPolicy::~MockRateLimitPolicy() {}

MockShadowWriter::MockShadowWriter() {}
MockShadowWriter::~MockShadowWriter() {}

MockVirtualHost::MockVirtualHost() {
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, rateLimitPolicy()).WillByDefault(ReturnRef(rate_limit_policy_));
  ON_CALL(*this, requestHeadersToAdd()).WillByDefault(ReturnRef(request_headers_to_add_));
}

MockVirtualHost::~MockVirtualHost() {}

MockHashPolicy::MockHashPolicy() {}
MockHashPolicy::~MockHashPolicy() {}

MockRouteEntry::MockRouteEntry() {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
  ON_CALL(*this, opaqueConfig()).WillByDefault(ReturnRef(opaque_config_));
  ON_CALL(*this, rateLimitPolicy()).WillByDefault(ReturnRef(rate_limit_policy_));
  ON_CALL(*this, retryPolicy()).WillByDefault(ReturnRef(retry_policy_));
  ON_CALL(*this, shadowPolicy()).WillByDefault(ReturnRef(shadow_policy_));
  ON_CALL(*this, timeout()).WillByDefault(Return(std::chrono::milliseconds(10)));
  ON_CALL(*this, virtualCluster(_)).WillByDefault(Return(&virtual_cluster_));
  ON_CALL(*this, virtualHost()).WillByDefault(ReturnRef(virtual_host_));
  ON_CALL(*this, requestHeadersToAdd()).WillByDefault(ReturnRef(request_headers_to_add_));
}

MockRouteEntry::~MockRouteEntry() {}

MockConfig::MockConfig() : route_(new NiceMock<MockRoute>()) {
  ON_CALL(*this, route(_, _)).WillByDefault(Return(route_));
  ON_CALL(*this, internalOnlyHeaders()).WillByDefault(ReturnRef(internal_only_headers_));
  ON_CALL(*this, responseHeadersToAdd()).WillByDefault(ReturnRef(response_headers_to_add_));
  ON_CALL(*this, responseHeadersToRemove()).WillByDefault(ReturnRef(response_headers_to_remove_));
  ON_CALL(*this, requestHeadersToAdd()).WillByDefault(ReturnRef(request_headers_to_add_));
}

MockConfig::~MockConfig() {}

MockRoute::MockRoute() { ON_CALL(*this, routeEntry()).WillByDefault(Return(&route_entry_)); }
MockRoute::~MockRoute() {}

} // Router
