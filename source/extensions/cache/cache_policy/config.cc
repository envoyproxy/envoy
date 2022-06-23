#include "source/extensions/cache/cache_policy/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
  namespace Extensions {
    namespace Cache {

static Registry::RegisterFactory<CachePolicyImplFactory, CachePolicyFactory> register_;

    } // namespace Cache
  } // namespace Extensions
} // namespace Envoy
