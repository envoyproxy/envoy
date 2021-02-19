#include "envoy/extensions/filters/common/dependency/v3/dependency.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

class DependencyManager {
public:
  DependencyManager() {}

  void RegisterFilter(
      std::string name,
      envoy::extensions::filters::common::dependency::v3::FilterDependencies dependencies) {
    filter_chain_.push_back({name, dependencies});
  }

  bool IsValid();

private:
  std::vector<std::pair<std::string,
                        envoy::extensions::filters::common::dependency::v3::FilterDependencies>>
      filter_chain_;
};

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
