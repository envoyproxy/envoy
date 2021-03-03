#pragma once

#include "envoy/extensions/filters/common/dependency/v3/dependency.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

/**
 * Validation for an http filter chain based on the factory-level
 * FilterDependencies specification. @see NamedHttpFilterConfigFactory
 * Currently only validates decode dependencies.
 */
class DependencyManager {
public:
  DependencyManager() = default;

  /**
   * Register each filter in an http filter chain, using name and dependencies
   * from the filter factory. Filters must be registered in decode path order.
   */
  void registerFilter(
      const std::string& name,
      const envoy::extensions::filters::common::dependency::v3::FilterDependencies& dependencies) {
    filter_chain_.push_back({name, dependencies});
  }

  /**
   * Returns true if the decode path of the filter chain is valid. A filter
   * chain is valid iff for each filter, every decode dependency has been
   * provided by a previous filter.
   *
   * TODO(auni53): Change this to a general valid() that checks decode and
   * encode path.
   */
  bool validDecodeDependencies();

private:
  std::vector<std::pair<std::string,
                        envoy::extensions::filters::common::dependency::v3::FilterDependencies>>
      filter_chain_;
};

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
