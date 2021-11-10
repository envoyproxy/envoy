#pragma once

#include "source/common/config/metadata.h"
#include "source/extensions/filters/network/meta_protocol_proxy/interface/route.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

class MockRetryPolicy : public RetryPolicy {
public:
  MockRetryPolicy() = default;

  MOCK_METHOD(bool, shouldRetry,
              (uint32_t count, const Response* response, absl::optional<Event> event), (const));
  MOCK_METHOD(std::chrono::milliseconds, timeout, (), (const));
};

class MockRouteEntry : public RouteEntry {
public:
  MockRouteEntry();

  MOCK_METHOD(const std::string&, clusterName, (), (const));
  MOCK_METHOD(const RouteSpecificFilterConfig*, perFilterConfig, (absl::string_view), (const));
  MOCK_METHOD(void, finalizeRequest, (Request & request), (const));
  MOCK_METHOD(void, finalizeResponse, (Response & response), (const));
  MOCK_METHOD(const Envoy::Config::TypedMetadata&, typedMetadata, (), (const));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, metadata, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, timeout, (), (const));
  MOCK_METHOD(const RetryPolicy&, retryPolicy, (), (const));

  std::string cluster_name_{"fake_cluster_name"};

  envoy::config::core::v3::Metadata metadata_;
  Envoy::Config::TypedMetadataImpl<RouteTypedMetadataFactory> typed_metadata_;
  testing::NiceMock<MockRetryPolicy> retry_policy_;
};

class MockRouteMatcher : public RouteMatcher {
public:
  MockRouteMatcher();

  MOCK_METHOD(RouteEntryConstSharedPtr, routeEntry, (const Request& request), (const));

  std::shared_ptr<const MockRouteEntry> route_entry_{std::make_shared<MockRouteEntry>()};
};

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
