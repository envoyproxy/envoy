#include "stream_shim.h"

#include "bytes_view.h"

namespace Envoy {
namespace Python {
namespace Stream {

Platform::Stream& send_data_shim(Platform::Stream& self, py::bytes data) {
  envoy_data raw_data = py_bytes_as_envoy_data(data);
  return self.send_data(raw_data);
}

void close_shim(Platform::Stream& self, py::bytes data) {
  envoy_data raw_data = py_bytes_as_envoy_data(data);
  self.close(raw_data);
}

} // namespace Stream
} // namespace Python
} // namespace Envoy
