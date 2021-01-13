#include "engine_builder_shim.h"

namespace py = pybind11;

namespace Envoy {
namespace Python {
namespace EngineBuilder {

Platform::EngineBuilder& set_on_engine_running_shim(Platform::EngineBuilder& self,
                                                    std::function<void()> closure) {
  return self.set_on_engine_running([closure]() {
    py::gil_scoped_acquire acquire;
    closure();
  });
}

} // namespace EngineBuilder
} // namespace Python
} // namespace Envoy
