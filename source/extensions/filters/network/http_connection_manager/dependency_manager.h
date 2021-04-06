#pragma once

#include "envoy/extensions/filters/common/dependency/v3/dependency.pb.h"

#include "absl/status/status.h"

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
   *
   * Currently, filters with no dependencies required or provided have no
   * bearing on the validity of a filter chain, and are silently skipped.
   */
  void registerFilter(
      const std::string& filter_name,
      const envoy::extensions::filters::common::dependency::v3::FilterDependencies& dependencies) {
    if (dependencies.ByteSizeLong() != 0) {
      filter_chain_.push_back({filter_name, dependencies});
    }
  }

  /**
   * Returns StatusCode::kOk if the decode path of the filter chain is valid.
   * A filter chain is valid iff for each filter, every decode dependency has
   * been provided by a previous filter.
   * Returns StatusCode::kNotFoundError if the decode path is invalid, with
   * details of the first dependency violation found.
   *
   * TODO(auni53): Change this to a general valid() that checks decode and
   * encode path.
   */
  absl::Status validDecodeDependencies();

  int filterChainSizeForTest() const {
    return filter_chain_.size();
  }

private:
  // Mapping of filter names to dependencies. This corresponds to a filter chain
  // configuration in decode-path order, but filters with no dependencies
  // required or provided are skipped.
  std::vector<std::pair<std::string,
                        envoy::extensions::filters::common::dependency::v3::FilterDependencies>>
      filter_chain_;
};

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
