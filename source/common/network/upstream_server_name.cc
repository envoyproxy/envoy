#include "common/network/upstream_server_name.h"

namespace Envoy {
namespace Network {

absl::string_view UpstreamServerName::key() {
  // Construct On First Use Idiom: https://isocpp.org/wiki/faq/ctors#static-init-order-on-first-use
  static const char* cstring_key = "envoy.network.upstream_server_name";
  return absl::string_view(cstring_key);
}
} // namespace Network
} // namespace Envoy
