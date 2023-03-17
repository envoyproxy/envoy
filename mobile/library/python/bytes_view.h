#pragma once

#include "library/common/types/c_types.h"
#include "pybind11/pybind11.h"

namespace py = pybind11;

namespace Envoy {
namespace Python {

envoy_data pyBytesAsEnvoyData(py::bytes bytes);
py::bytes envoyDataAsPyBytes(envoy_data data);

} // namespace Python
} // namespace Envoy
