#pragma once

#include <functional>

#include "library/cc/stream_prototype.h"
#include "library/common/types/c_types.h"
#include "pybind11/pybind11.h"

namespace py = pybind11;

namespace Envoy {
namespace Python {
namespace StreamPrototype {

using OnPyBytesDataCallback = std::function<void(py::bytes bytes, bool end_stream)>;

// each of these shims exist to insert a GIL aqcuisition between the C++
// callback and the call into Python code.
Platform::StreamPrototype& set_on_headers_shim(Platform::StreamPrototype& self,
                                               Platform::OnHeadersCallback closure);

Platform::StreamPrototype& set_on_data_shim(Platform::StreamPrototype& self,
                                            OnPyBytesDataCallback closure);

Platform::StreamPrototype& set_on_trailers_shim(Platform::StreamPrototype& self,
                                                Platform::OnTrailersCallback closure);

Platform::StreamPrototype& set_on_error_shim(Platform::StreamPrototype& self,
                                             Platform::OnErrorCallback closure);

Platform::StreamPrototype& set_on_complete_shim(Platform::StreamPrototype& self,
                                                Platform::OnCompleteCallback closure);

Platform::StreamPrototype& set_on_cancel_shim(Platform::StreamPrototype& self,
                                              Platform::OnCancelCallback closure);

} // namespace StreamPrototype
} // namespace Python
} // namespace Envoy
