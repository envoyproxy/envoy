#include "contrib/golang/http/cluster_specifier/source/golang_cluster_specifier.h"

namespace Envoy {
namespace Extensions {
namespace ClusterSpecifier {
namespace Golang {

//
// These functions should only be invoked in the current Envoy worker thread.
//

extern "C" {

void envoyGoClusterSpecifierGetHeader(unsigned long long headerPtr, void* key, void* value) {}
void envoyGoClusterSpecifierCopyHeaders(unsigned long long headerPtr, void* strs, void* buf) {}
void envoyGoClusterSpecifierSetHeader(unsigned long long headerPtr, void* key, void* value) {}
void envoyGoClusterSpecifierRemoveHeader(unsigned long long headerPtr, void* key) {}
}

} // namespace Golang
} // namespace ClusterSpecifier
} // namespace Extensions
} // namespace Envoy
