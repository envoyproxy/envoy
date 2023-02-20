#include "contrib/golang/http/cluster_specifier/source/golang_cluster_specifier.h"

namespace Envoy {
namespace Extensions {
namespace ClusterSpecifier {
namespace Golang {

//
// These functions should only be invoked in the current Envoy worker thread.
//

absl::string_view referGoString(void* str) {
  if (str == nullptr) {
    return "";
  }
  auto goStr = reinterpret_cast<GoString*>(str);
  return absl::string_view(goStr->p, goStr->n); // NOLINT(modernize-return-braced-init-list)
}

extern "C" {

void envoyGoClusterSpecifierGetHeader(unsigned long long headerPtr, void* key, void* value) {
  auto header = reinterpret_cast<Http::RequestHeaderMap*>(headerPtr);
  auto keyStr = referGoString(key);
  auto goValue = reinterpret_cast<GoString*>(value);
  auto result = header->get(Http::LowerCaseString(keyStr));

  if (!result.empty()) {
    auto str = result[0]->value().getStringView();
    goValue->p = str.data();
    goValue->n = str.length();
  }
}
}

} // namespace Golang
} // namespace ClusterSpecifier
} // namespace Extensions
} // namespace Envoy
