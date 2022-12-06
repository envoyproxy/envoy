#pragma once

#include <chrono>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/typed_metadata.h"
#include "envoy/rds/config.h"

#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

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
 * Interface of typed metadata factory.
 */
class RouteTypedMetadataFactory : public Envoy::Config::TypedMetadataFactory {};

class RouteEntry {
public:
  virtual ~RouteEntry() = default;

  virtual const std::string& clusterName() const PURE;

  /**
   * Get route level per filter config by the filter name.
   */
  virtual const RouteSpecificFilterConfig* perFilterConfig(absl::string_view) const PURE;
  template <class T> const T* typedPerFilterConfig(absl::string_view name) const {
    return dynamic_cast<const T*>(perFilterConfig(name));
  }

  /**
   * @return const envoy::config::core::v3::Metadata& return the metadata provided in the config for
   * this route.
   */
  virtual const envoy::config::core::v3::Metadata& metadata() const PURE;
};
using RouteEntryConstSharedPtr = std::shared_ptr<const RouteEntry>;

class RouteMatcher : public Rds::Config {
public:
  virtual RouteEntryConstSharedPtr routeEntry(const Request& request) const PURE;
};
using RouteMatcherPtr = std::unique_ptr<RouteMatcher>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
