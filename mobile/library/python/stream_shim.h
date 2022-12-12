#pragma once

#include "library/cc/stream.h"
#include "pybind11/pybind11.h"

namespace py = pybind11;

namespace Envoy {
namespace Python {
namespace Stream {

Platform::Stream& sendDataShim(Platform::Stream& self, py::bytes data);
void closeShim(Platform::Stream& self, py::bytes data);

} // namespace Stream
} // namespace Python
} // namespace Envoy
