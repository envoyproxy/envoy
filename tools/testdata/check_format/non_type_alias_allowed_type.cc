#include <memory>
#include <string>
#include <vector>

namespace Envoy {

void a(std::unique_ptr<int>, std::shared_ptr<std::string>,
       absl::optional<std::reference_wrapper<char[]>>,
       absl::optional<std::reference_wrapper<std::vector<int>>>) {}

} // namespace Envoy