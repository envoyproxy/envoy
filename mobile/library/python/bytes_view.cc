#include "bytes_view.h"

namespace Envoy {
namespace Python {

struct EnvoyDataViewContext {
  py::bytes handle;
};

static void releaseEnvoyDataView(void* context) {
  auto envoy_data_view_context = static_cast<EnvoyDataViewContext*>(context);
  delete envoy_data_view_context;
}

envoy_data pyBytesAsEnvoyData(py::bytes bytes) {
  auto context = new EnvoyDataViewContext{bytes};
  py::buffer_info info(py::buffer(bytes).request());
  return envoy_data{
      .length = static_cast<size_t>(info.itemsize * info.size),
      .bytes = static_cast<uint8_t*>(info.ptr),
      .release = releaseEnvoyDataView,
      .context = static_cast<void*>(context),
  };
}

// we cannot extend the lifetime of an envoy_data like we can a py::bytes
// so instead of providing a view onto the underlying data
// we just copy it over to python-land.
py::bytes envoyDataAsPyBytes(envoy_data data) {
  auto bytes = py::bytes((const char*)data.bytes, data.length);
  release_envoy_data(data);
  return bytes;
}

} // namespace Python
} // namespace Envoy
