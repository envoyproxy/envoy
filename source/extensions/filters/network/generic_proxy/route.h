#pragma once

#include <chrono>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/rds/config.h"
#include "envoy/router/router.h"

#include "source/extensions/filters/network/generic_proxy/interface/stream.h"
#include "source/extensions/filters/network/generic_proxy/match_input.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class RouteSpecificFilterConfig {
public:
  virtual ~RouteSpecificFilterConfig() = default;
};
using RouteSpecificFilterConfigConstSharedPtr = std::shared_ptr<const RouteSpecificFilterConfig>;

/**
 * Interface of typed metadata factory. Reuse the same interface as the HTTP router filter because
 * part of these abstractions are protocol independent.
 */
using RouteTypedMetadataFactory = Envoy::Router::HttpRouteTypedMetadataFactory;

/**
 * The simplest retry implementation. It only contains the number of retries.
 */
class RetryPolicy {
public:
  RetryPolicy(uint32_t num_retries) : num_retries_(num_retries) {}
  uint32_t numRetries() const { return num_retries_; }

private:
  const uint32_t num_retries_{};
};

class RouteEntry {
public:
  virtual ~RouteEntry() = default;

  /**
   * @return absl::string_view the name of the route.
   */
  virtual absl::string_view name() const PURE;

  /**
   * @return const std::string& the name of the target cluster.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * Get route level per filter config by the filter name.
   */
  virtual const RouteSpecificFilterConfig* perFilterConfig(absl::string_view) const PURE;
  template <class T> const T* typedPerFilterConfig(absl::string_view name) const {
    return dynamic_cast<const T*>(perFilterConfig(name));
  }

  /**
   * @return const envoy::config::core::v3::Metadata& return the metadata provided in the config
   * for this route.
   */
  virtual const envoy::config::core::v3::Metadata& metadata() const PURE;

  /**
   * @return const Envoy::Config::TypedMetadata& return the typed metadata provided in the config
   * for this route.
   */
  virtual const Envoy::Config::TypedMetadata& typedMetadata() const PURE;

  /**
   * @return route timeout for this route.
   */
  virtual const std::chrono::milliseconds timeout() const PURE;

  /**
   * @return const RetryPolicy& the retry policy for this route.
   */
  virtual const RetryPolicy& retryPolicy() const PURE;
};

using RouteEntryConstSharedPtr = std::shared_ptr<const RouteEntry>;

class RouteMatcher : public Rds::Config {
public:
  virtual RouteEntryConstSharedPtr routeEntry(const MatchInput& request) const PURE;
};
using RouteMatcherPtr = std::unique_ptr<RouteMatcher>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
