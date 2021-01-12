#include "bytes_view.h"

namespace Envoy {
namespace Python {

struct EnvoyDataViewContext {
  const py::bytes& handle;
};

static void release_envoy_data_view(void* context) {
  auto envoy_data_view_context = static_cast<EnvoyDataViewContext*>(context);
  envoy_data_view_context->handle.dec_ref();
}

envoy_data py_bytes_as_envoy_data(const py::bytes& bytes) {
  auto context = new EnvoyDataViewContext{
      .handle = bytes,
  };
  bytes.inc_ref();

  py::buffer_info info(py::buffer(bytes).request());
  return envoy_data{
      .length = static_cast<size_t>(info.itemsize * info.size),
      .bytes = static_cast<uint8_t*>(info.ptr),
      .release = release_envoy_data_view,
      .context = static_cast<void*>(context),
  };
}

// we cannot extend the lifetime of an envoy_data like we can a py::bytes
// so instead of providing a view onto the underlying data
// we just copy it over to python-land.
py::bytes envoy_data_as_py_bytes(envoy_data data) {
  auto bytes = py::bytes((const char*)data.bytes, data.length);
  data.release(data.context);
  return bytes;
}

} // namespace Python
} // namespace Envoy
