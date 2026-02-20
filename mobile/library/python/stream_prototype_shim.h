#pragma once

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>

#include "library/cc/stream.h"
#include "library/cc/stream_prototype.h"
#include "library/common/engine_types.h"

namespace py = pybind11;

namespace Envoy {
namespace Python {

// Python-friendly start() that takes individual Python callables and wraps them
// with GIL acquisition and C++-to-Python type conversion.
Platform::StreamSharedPtr
startStreamShim(Platform::StreamPrototype& self,
                py::object on_headers,  // (dict, bool, StreamIntel) -> None
                py::object on_data,     // (bytes, int, bool, StreamIntel) -> None
                py::object on_trailers, // (dict, StreamIntel) -> None
                py::object on_complete, // (StreamIntel, FinalStreamIntel) -> None
                py::object on_error,    // (EnvoyError, StreamIntel, FinalStreamIntel) -> None
                py::object on_cancel,   // (StreamIntel, FinalStreamIntel) -> None
                bool explicit_flow_control);

} // namespace Python
} // namespace Envoy
