#include "stream_shim.h"

#include "bytes_view.h"

namespace Envoy {
namespace Python {
namespace Stream {

Platform::Stream& sendDataShim(Platform::Stream& self, py::bytes data) {
  envoy_data raw_data = pyBytesAsEnvoyData(data);
  return self.sendData(raw_data);
}

void closeShim(Platform::Stream& self, py::bytes data) {
  envoy_data raw_data = pyBytesAsEnvoyData(data);
  self.close(raw_data);
}

} // namespace Stream
} // namespace Python
} // namespace Envoy
