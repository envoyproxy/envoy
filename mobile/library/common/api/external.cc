#include "external.h"

#include "common/common/assert.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Api {
namespace External {

// TODO(goaway): This needs to be updated not to leak once multiple engines are supported.
// See https://github.com/lyft/envoy-mobile/issues/332
static absl::flat_hash_map<std::string, void*> registry_{};

// TODO(goaway): To expose this for general usage, it will need to be made thread-safe. For now it
// relies on the assumption that usage will occur only as part of Engine configuration, and thus be
// limited to a single thread.
void registerApi(std::string name, void* api) { registry_[name] = api; }

// TODO(goaway): This is not thread-safe, but the assumption here is that all writes will complete
// before any reads occur.
void* retrieveApi(std::string name) {
  void* api = registry_[name];
  ASSERT(api);
  return api;
}

} // namespace External
} // namespace Api
} // namespace Envoy
