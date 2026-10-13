#pragma once

#include "source/common/config/metadata.h"
#include "source/extensions/filters/network/generic_proxy/route.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();

  MOCK_METHOD(const std::string&, clusterName, (), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, perFilterConfig, (absl::string_view), (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const));
  MOCK_METHOD(absl::string_view, name, (), (const));
  MOCK_METHOD(const std::chrono::milliseconds, timeout, (), (const));
  MOCK_METHOD(const RetryPolicy&, retryPolicy, (), (const));

  std::string cluster_name_{"fake_cluster_name"};

  envoy::config::core::v3::Metadata metadata_;

  std::chrono::milliseconds timeout_;
  RetryPolicy retry_policy_{1};
};

class MockRouteMatcher : public RouteMatcher {
public:
  MockRouteMatcher();

  MOCK_METHOD(RouteEntryConstSharedPtr, routeEntry, (const MatchInput& request), (const));

  std::shared_ptr<const testing::NiceMock<MockRouteEntry>> route_entry_{
      std::make_shared<testing::NiceMock<MockRouteEntry>>()};
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
