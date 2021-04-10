#pragma once

#include "library/common/types/c_types.h"
#include "pybind11/pybind11.h"

namespace py = pybind11;

namespace Envoy {
namespace Python {

envoy_data py_bytes_as_envoy_data(py::bytes bytes);
py::bytes envoy_data_as_py_bytes(envoy_data data);

} // namespace Python
} // namespace Envoy
