#include "contrib/golang/router/cluster_specifier/source/golang_cluster_specifier.h"

namespace Envoy {
namespace Router {
namespace Golang {

//
// These functions should only be invoked in the current Envoy worker thread.
//

enum GetHeaderResult {
  Mising = 0,
  Found = 1,
};

absl::string_view referGoString(void* str) {
  if (str == nullptr) {
    return "";
  }
  auto go_str = reinterpret_cast<GoString*>(str);
  return absl::string_view(go_str->p, go_str->n); // NOLINT(modernize-return-braced-init-list)
}

#ifdef __cplusplus
extern "C" {
#endif

// Get the value of the specified header key from the request header map.
// Only use the first value when there are multiple values associated with the key.
int envoyGoClusterSpecifierGetHeader(unsigned long long header_ptr, void* key, void* value) {
  auto header = reinterpret_cast<Http::RequestHeaderMap*>(header_ptr);
  auto key_str = referGoString(key);
  auto go_value = reinterpret_cast<GoString*>(value);
  auto result = header->get(Http::LowerCaseString(key_str));

  if (!result.empty()) {
    auto str = result[0]->value().getStringView();
    go_value->p = str.data();
    go_value->n = str.length();
    return static_cast<int>(GetHeaderResult::Found);
  }
  return static_cast<int>(GetHeaderResult::Mising);
}

// Log the message with the error level.
void envoyGoClusterSpecifierLogError(unsigned long long plugin_ptr, void* msg) {
  auto msgStr = referGoString(msg);
  auto plugin = reinterpret_cast<GolangClusterSpecifierPlugin*>(plugin_ptr);
  plugin->log(msgStr);
}

#ifdef __cplusplus
}
#endif

} // namespace Golang
} // namespace Router
} // namespace Envoy
