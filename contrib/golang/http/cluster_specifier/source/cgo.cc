#include "contrib/golang/http/cluster_specifier/source/golang_cluster_specifier.h"

namespace Envoy {
namespace Extensions {
namespace ClusterSpecifier {
namespace Golang {

//
// These functions should only be invoked in the current Envoy worker thread.
//

extern "C" {

void envoyGoClusterSpecifierGetHeader(unsigned long long headerPtr, void* key, void* value) {
  UNREFERENCED_PARAMETER(headerPtr);
  UNREFERENCED_PARAMETER(key);
  UNREFERENCED_PARAMETER(value);
}

void envoyGoClusterSpecifierCopyHeaders(unsigned long long headerPtr, void* strs, void* buf) {
  UNREFERENCED_PARAMETER(headerPtr);
  UNREFERENCED_PARAMETER(strs);
  UNREFERENCED_PARAMETER(buf);
}

void envoyGoClusterSpecifierSetHeader(unsigned long long headerPtr, void* key, void* value) {
  UNREFERENCED_PARAMETER(headerPtr);
  UNREFERENCED_PARAMETER(key);
  UNREFERENCED_PARAMETER(value);
}

void envoyGoClusterSpecifierRemoveHeader(unsigned long long headerPtr, void* key) {
  UNREFERENCED_PARAMETER(headerPtr);
  UNREFERENCED_PARAMETER(key);
}
}

} // namespace Golang
} // namespace ClusterSpecifier
} // namespace Extensions
} // namespace Envoy
