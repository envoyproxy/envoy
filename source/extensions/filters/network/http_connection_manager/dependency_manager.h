#include "envoy/extensions/filters/common/dependency/v3/dependency.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

/**
 * Validation for an http filter chain based on the factory-level
 * FilterDependencies specification. Currently only validates decode
 * dependencies.
 * @see NamedHttpFilterConfigFactory
 */
class DependencyManager {
public:
  DependencyManager() {}

  /**
   * Register each filter in an http filterchain, using name and dependencies
   * from the filter factory. Filters must be registered in decode path order.
   */
  void RegisterFilter(
      std::string name,
      envoy::extensions::filters::common::dependency::v3::FilterDependencies dependencies) {
    filter_chain_.push_back({name, dependencies});
  }

  /**
   * Returns true if the decode path of the filterchain is valid. A filterchain
   * is valid iff for each filter, any decode dependencies required have been
   * provided by a previous filter.
   *
   * TODO(auni53): Change this to a general IsValid() that checks decode and
   * encode path.
   */
  bool DecodePathIsValid();

private:
  std::vector<std::pair<std::string,
                        envoy::extensions::filters::common::dependency::v3::FilterDependencies>>
      filter_chain_;
};

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
