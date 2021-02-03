#include "class_defn.h"

// NOLINT(namespace-envoy)

namespace {
Foo Bar::getFoo() {
  Foo foo;
  return foo;
}

int DeadBeaf::val() { return 42; }

DeadBeaf::DeadBeaf() = default;
} // namespace