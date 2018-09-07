#include "envoy/registry/registry.h"

#include "test/integration/test_host_predicate_config.h"

namespace Envoy {
static Registry::RegisterFactory<TestHostPredicateFactory, Upstream::RetryHostPredicateFactory> register_;
} // namespace Envoy
