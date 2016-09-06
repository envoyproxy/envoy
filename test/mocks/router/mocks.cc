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

MockShadowWriter::MockShadowWriter() {}
MockShadowWriter::~MockShadowWriter() {}

MockRouteEntry::MockRouteEntry() {
  ON_CALL(*this, clusterName()).WillByDefault(ReturnRef(cluster_name_));
  ON_CALL(*this, rateLimitPolicy()).WillByDefault(ReturnRef(rate_limit_policy_));
  ON_CALL(*this, retryPolicy()).WillByDefault(ReturnRef(retry_policy_));
  ON_CALL(*this, shadowPolicy()).WillByDefault(ReturnRef(shadow_policy_));
  ON_CALL(*this, timeout()).WillByDefault(Return(std::chrono::milliseconds(10)));
  ON_CALL(*this, virtualCluster(_)).WillByDefault(Return(&virtual_cluster_));
  ON_CALL(*this, virtualHostName()).WillByDefault(ReturnRef(vhost_name_));
}

MockRouteEntry::~MockRouteEntry() {}

MockConfig::MockConfig() {
  ON_CALL(*this, internalOnlyHeaders()).WillByDefault(ReturnRef(internal_only_headers_));
  ON_CALL(*this, responseHeadersToAdd()).WillByDefault(ReturnRef(response_headers_to_add_));
  ON_CALL(*this, responseHeadersToRemove()).WillByDefault(ReturnRef(response_headers_to_remove_));
}

MockConfig::~MockConfig() {}

MockStableRouteTable::MockStableRouteTable() {
  ON_CALL(*this, routeForRequest(_)).WillByDefault(Return(&route_entry_));
}

MockStableRouteTable::~MockStableRouteTable() {}

} // Router
