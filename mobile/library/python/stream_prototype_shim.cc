#include "library/python/stream_prototype_shim.h"

#include <pybind11/stl.h>

namespace Envoy {
namespace Python {

namespace {

// Custom deleter that acquires the GIL before destroying a py::object.
// Required because these shared_ptrs may be destroyed on Envoy's network thread.
void gilSafeDeletePyObject(py::object* p) {
  py::gil_scoped_acquire acquire;
  delete p;
}

// Creates a shared_ptr<py::object> with a GIL-safe deleter.
std::shared_ptr<py::object> makeGilSafeCallback(py::object obj) {
  return std::shared_ptr<py::object>(new py::object(std::move(obj)), gilSafeDeletePyObject);
}

// Converts Http::HeaderMap to a Python dict of {str: list[str]}.
// Caller must hold the GIL.
py::dict headerMapToDict(const Http::HeaderMap& headers) {
  py::dict result;
  headers.iterate([&result](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
    std::string key(entry.key().getStringView());
    std::string value(entry.value().getStringView());
    if (result.contains(key)) {
      result[py::str(key)].cast<py::list>().append(py::str(value));
    } else {
      py::list values;
      values.append(py::str(value));
      result[py::str(key)] = values;
    }
    return Http::HeaderMap::Iterate::Continue;
  });
  return result;
}

} // namespace

Platform::StreamSharedPtr startStreamShim(Platform::StreamPrototype& self, py::object on_headers,
                                          py::object on_data, py::object on_trailers,
                                          py::object on_complete, py::object on_error,
                                          py::object on_cancel, bool explicit_flow_control) {
  EnvoyStreamCallbacks callbacks;

  if (!on_headers.is_none()) {
    auto shared_cb = makeGilSafeCallback(on_headers);
    callbacks.on_headers_ = [shared_cb](const Http::ResponseHeaderMap& headers, bool end_stream,
                                        envoy_stream_intel intel) {
      py::gil_scoped_acquire acquire;
      py::dict py_headers = headerMapToDict(headers);
      (*shared_cb)(py_headers, end_stream, intel);
    };
  }

  if (!on_data.is_none()) {
    auto shared_cb = makeGilSafeCallback(on_data);
    callbacks.on_data_ = [shared_cb](const Buffer::Instance& buffer, uint64_t length,
                                     bool end_stream, envoy_stream_intel intel) {
      py::gil_scoped_acquire acquire;
      // Extract buffer contents as Python bytes.
      std::string data(length, '\0');
      buffer.copyOut(0, length, data.data());
      (*shared_cb)(py::bytes(data), length, end_stream, intel);
    };
  }

  if (!on_trailers.is_none()) {
    auto shared_cb = makeGilSafeCallback(on_trailers);
    callbacks.on_trailers_ = [shared_cb](const Http::ResponseTrailerMap& trailers,
                                         envoy_stream_intel intel) {
      py::gil_scoped_acquire acquire;
      py::dict py_trailers = headerMapToDict(trailers);
      (*shared_cb)(py_trailers, intel);
    };
  }

  if (!on_complete.is_none()) {
    auto shared_cb = makeGilSafeCallback(on_complete);
    callbacks.on_complete_ = [shared_cb](envoy_stream_intel intel,
                                         envoy_final_stream_intel final_intel) {
      py::gil_scoped_acquire acquire;
      (*shared_cb)(intel, final_intel);
    };
  }

  if (!on_error.is_none()) {
    auto shared_cb = makeGilSafeCallback(on_error);
    callbacks.on_error_ = [shared_cb](const EnvoyError& error, envoy_stream_intel intel,
                                      envoy_final_stream_intel final_intel) {
      py::gil_scoped_acquire acquire;
      (*shared_cb)(error, intel, final_intel);
    };
  }

  if (!on_cancel.is_none()) {
    auto shared_cb = makeGilSafeCallback(on_cancel);
    callbacks.on_cancel_ = [shared_cb](envoy_stream_intel intel,
                                       envoy_final_stream_intel final_intel) {
      py::gil_scoped_acquire acquire;
      (*shared_cb)(intel, final_intel);
    };
  }

  py::gil_scoped_release release;
  return self.start(std::move(callbacks), explicit_flow_control);
}

} // namespace Python
} // namespace Envoy
