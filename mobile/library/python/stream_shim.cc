#include "library/python/stream_shim.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Python {

namespace {

// Converts a Python dict to Http::RequestHeaderMapPtr.
// Accepts {str: str} or {str: list[str]} values.
Http::RequestHeaderMapPtr dictToRequestHeaders(py::dict headers) {
  auto header_map = Http::createHeaderMap<Http::RequestHeaderMapImpl>({});
  for (auto item : headers) {
    std::string key = py::str(item.first);
    auto value_obj = py::reinterpret_borrow<py::object>(item.second);
    if (py::isinstance<py::list>(value_obj)) {
      for (auto val : value_obj.cast<py::list>()) {
        header_map->addCopy(Http::LowerCaseString(key), py::str(val).cast<std::string>());
      }
    } else {
      header_map->addCopy(Http::LowerCaseString(key), py::str(value_obj).cast<std::string>());
    }
  }
  return header_map;
}

// Converts a Python dict to Http::RequestTrailerMapPtr.
Http::RequestTrailerMapPtr dictToRequestTrailers(py::dict trailers) {
  auto trailer_map = Http::createHeaderMap<Http::RequestTrailerMapImpl>({});
  for (auto item : trailers) {
    std::string key = py::str(item.first);
    auto value_obj = py::reinterpret_borrow<py::object>(item.second);
    if (py::isinstance<py::list>(value_obj)) {
      for (auto val : value_obj.cast<py::list>()) {
        trailer_map->addCopy(Http::LowerCaseString(key), py::str(val).cast<std::string>());
      }
    } else {
      trailer_map->addCopy(Http::LowerCaseString(key), py::str(value_obj).cast<std::string>());
    }
  }
  return trailer_map;
}

// Converts Python bytes to Buffer::InstancePtr.
Buffer::InstancePtr bytesToBuffer(py::bytes data) {
  std::string data_str = data;
  auto buffer = std::make_unique<Buffer::OwnedImpl>();
  buffer->add(data_str);
  return buffer;
}

} // namespace

Platform::Stream& sendHeadersShim(Platform::Stream& self, py::dict headers, bool end_stream,
                                  bool idempotent) {
  auto header_map = dictToRequestHeaders(headers);
  py::gil_scoped_release release;
  return self.sendHeaders(std::move(header_map), end_stream, idempotent);
}

Platform::Stream& sendDataShim(Platform::Stream& self, py::bytes data) {
  auto buffer = bytesToBuffer(data);
  py::gil_scoped_release release;
  return self.sendData(std::move(buffer));
}

void closeWithDataShim(Platform::Stream& self, py::bytes data) {
  auto buffer = bytesToBuffer(data);
  py::gil_scoped_release release;
  self.close(std::move(buffer));
}

void closeWithTrailersShim(Platform::Stream& self, py::dict trailers) {
  auto trailer_map = dictToRequestTrailers(trailers);
  py::gil_scoped_release release;
  self.close(std::move(trailer_map));
}

} // namespace Python
} // namespace Envoy
