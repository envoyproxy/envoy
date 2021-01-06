#pragma once

#include <memory>

namespace Envoy {
namespace Platform {

// TODO(crockeo): although this is stubbed out since it's in the main directory, it depends on
// objects defined under stats. this will not be fully stubbed until stats is stubbed

class PulseClient {
public:
  // TODO: when these are commented out, you're going to have to
  // #include "envoy/common/pure.h"
  //
  // virtual Counter counter(Element element) PURE;
  // virtual Gauge gauge(Element element) PURE;
};

using PulseClientSharedPtr = std::shared_ptr<PulseClient>;

} // namespace Platform
} // namespace Envoy
