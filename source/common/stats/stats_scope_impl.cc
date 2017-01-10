#include "stats_scope_impl.h"

namespace Stats {

Counter& ScopeImpl::counter(const std::string& name) {
  // TODO: Ref-counting.
  return parent_.counter(prefix_ + name);
}

Gauge& ScopeImpl::gauge(const std::string& name) {
  // TODO: Ref-counting.
  return parent_.gauge(prefix_ + name);
}

Timer& ScopeImpl::timer(const std::string& name) {
  // TODO: Ref-counting.
  return parent_.timer(prefix_ + name);
}

} // Stats
