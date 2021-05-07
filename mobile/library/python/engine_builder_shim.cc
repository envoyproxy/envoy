#include "engine_builder_shim.h"

namespace py = pybind11;

namespace Envoy {
namespace Python {
namespace EngineBuilder {

Platform::EngineBuilder& setOnEngineRunningShim(Platform::EngineBuilder& self,
                                                std::function<void()> closure) {
  return self.setOnEngineRunning([closure]() {
    py::gil_scoped_acquire acquire;
    closure();
  });
}

} // namespace EngineBuilder
} // namespace Python
} // namespace Envoy
