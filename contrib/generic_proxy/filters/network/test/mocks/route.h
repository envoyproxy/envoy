#pragma once

#include "source/common/config/metadata.h"

#include "contrib/generic_proxy/filters/network/source/interface/route.h"
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

  std::string cluster_name_{"fake_cluster_name"};

  envoy::config::core::v3::Metadata metadata_;
};

class MockRouteMatcher : public RouteMatcher {
public:
  MockRouteMatcher();

  MOCK_METHOD(RouteEntryConstSharedPtr, routeEntry, (const Request& request), (const));

  std::shared_ptr<const MockRouteEntry> route_entry_{std::make_shared<MockRouteEntry>()};
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
