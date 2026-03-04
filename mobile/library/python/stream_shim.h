#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "library/cc/stream.h"

namespace py = pybind11;

namespace Envoy {
namespace Python {

// Converts a Python dict of {str: str} or {str: list[str]} to
// Http::RequestHeaderMapPtr and sends headers on the stream.
Platform::Stream& sendHeadersShim(Platform::Stream& self, py::dict headers, bool end_stream,
                                  bool idempotent = false);

// Converts Python bytes to Buffer::InstancePtr and sends data on the stream.
Platform::Stream& sendDataShim(Platform::Stream& self, py::bytes data);

// Converts Python bytes to Buffer::InstancePtr and closes the stream with data.
void closeWithDataShim(Platform::Stream& self, py::bytes data);

// Converts a Python dict to Http::RequestTrailerMapPtr and closes the stream with trailers.
void closeWithTrailersShim(Platform::Stream& self, py::dict trailers);

} // namespace Python
} // namespace Envoy
