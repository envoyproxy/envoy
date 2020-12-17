#pragma once

// NOLINT(namespace-envoy)

#include <memory>

// TODO(crockeo): although this is stubbed out since it's in the main directory, it depends on
// objects defined under stats. this will not be fully stubbed until stats is stubbed

// #include "stats/counter.h"
// #include "stats/element.h"
// #include "stats/gauge.h"

class PulseClient {
public:
  // virtual Counter counter(Element element) = 0;
  // virtual Gauge gauge(Element element) = 0;
};

using PulseClientSharedPtr = std::shared_ptr<PulseClient>;
