#include "library/python/engine_builder_shim.h"

#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace Envoy {
namespace Python {

namespace {

// Custom deleter that acquires the GIL before destroying a std::function holding
// a Python callable. Without this, the destructor would decref the Python object
// on Envoy's network thread without the GIL, triggering a pybind11 assertion.
void gilSafeDelete(std::function<void()>* p) {
  py::gil_scoped_acquire acquire;
  delete p;
}

} // namespace

Platform::EngineBuilder& setOnEngineRunningShim(Platform::EngineBuilder& self,
                                                std::function<void()> closure) {
  std::shared_ptr<std::function<void()>> shared_closure(
      new std::function<void()>(std::move(closure)), gilSafeDelete);
  return self.setOnEngineRunning([shared_closure]() {
    py::gil_scoped_acquire acquire;
    (*shared_closure)();
  });
}

Platform::EngineBuilder& setOnEngineExitShim(Platform::EngineBuilder& self,
                                             std::function<void()> closure) {
  std::shared_ptr<std::function<void()>> shared_closure(
      new std::function<void()>(std::move(closure)), gilSafeDelete);
  return self.setOnEngineExit([shared_closure]() {
    py::gil_scoped_acquire acquire;
    (*shared_closure)();
  });
}

} // namespace Python
} // namespace Envoy
