#include "source/common/router/delegating_route_impl.h"

#include "test/mocks/router/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

using testing::Return;

#define TEST_METHOD(method, ...)                                                                   \
  EXPECT_CALL(*inner_object_ptr, method);                                                          \
  wrapper_object.method(__VA_ARGS__);

// Verify that DelegatingRoute class forwards all calls to internal base route.
TEST(DelegatingRoute, DelegatingRouteTest) {
  const std::shared_ptr<MockRoute> inner_object_ptr = std::make_shared<MockRoute>();
  DelegatingRoute wrapper_object(inner_object_ptr);

  TEST_METHOD(directResponseEntry);
  TEST_METHOD(routeEntry);
  TEST_METHOD(decorator);
  TEST_METHOD(tracingConfig);
  TEST_METHOD(metadata);
  TEST_METHOD(typedMetadata);
  TEST_METHOD(routeName);
  TEST_METHOD(virtualHost);

  std::string name;
  TEST_METHOD(mostSpecificPerFilterConfig, name);

  TEST_METHOD(perFilterConfigs, name);
}

// Verify that DelegatingRouteEntry class forwards all calls to internal base route.
TEST(DelegatingRouteEntry, DelegatingRouteEntryTest) {
  const std::shared_ptr<MockRoute> base_route_ptr = std::make_shared<MockRoute>();
  const std::shared_ptr<MockRouteEntry> inner_object_ptr = std::make_shared<MockRouteEntry>();
  DelegatingRouteEntry wrapper_object(base_route_ptr);
  EXPECT_CALL(*base_route_ptr, routeEntry).WillRepeatedly(Return(inner_object_ptr.get()));

  Http::TestRequestHeaderMapImpl request_headers;
  Http::TestResponseHeaderMapImpl response_headers;
  StreamInfo::MockStreamInfo stream_info;

  TEST_METHOD(finalizeResponseHeaders, response_headers, stream_info);
  TEST_METHOD(responseHeaderTransforms, stream_info);
  TEST_METHOD(clusterName);
  TEST_METHOD(clusterNotFoundResponseCode);
  TEST_METHOD(corsPolicy);
  TEST_METHOD(currentUrlPathAfterRewrite, request_headers);
  TEST_METHOD(finalizeRequestHeaders, request_headers, stream_info, true);
  TEST_METHOD(requestHeaderTransforms, stream_info);
  TEST_METHOD(hashPolicy);
  TEST_METHOD(hedgePolicy);
  TEST_METHOD(priority);
  TEST_METHOD(rateLimitPolicy);
  TEST_METHOD(retryPolicy);
  TEST_METHOD(pathMatcher);
  TEST_METHOD(pathRewriter);
  TEST_METHOD(internalRedirectPolicy);
  TEST_METHOD(retryShadowBufferLimit);
  TEST_METHOD(shadowPolicies);
  TEST_METHOD(timeout);
  TEST_METHOD(idleTimeout);
  TEST_METHOD(usingNewTimeouts);
  TEST_METHOD(maxStreamDuration);
  TEST_METHOD(grpcTimeoutHeaderMax);
  TEST_METHOD(grpcTimeoutHeaderOffset);
  TEST_METHOD(maxGrpcTimeout);
  TEST_METHOD(grpcTimeoutOffset);
  TEST_METHOD(autoHostRewrite);
  TEST_METHOD(appendXfh);
  TEST_METHOD(metadataMatchCriteria);
  TEST_METHOD(opaqueConfig);
  TEST_METHOD(includeVirtualHostRateLimits);
  TEST_METHOD(tlsContextMatchCriteria);
  TEST_METHOD(pathMatchCriterion);
  TEST_METHOD(includeAttemptCountInRequest);
  TEST_METHOD(includeAttemptCountInResponse);
  TEST_METHOD(upgradeMap);
  TEST_METHOD(connectConfig);
  TEST_METHOD(earlyDataPolicy);
  TEST_METHOD(routeStatsContext);
}

} // namespace
} // namespace Router
} // namespace Envoy
