#include "stream_prototype_shim.h"

#include "bytes_view.h"

namespace Envoy {
namespace Python {
namespace StreamPrototype {

template <typename... Ts> std::function<void(Ts...)> make_shim(std::function<void(Ts...)> closure) {
  return [closure](Ts... args) {
    py::gil_scoped_acquire acquire;
    closure(std::forward<Ts>(args)...);
  };
}

Platform::StreamPrototype& set_on_headers_shim(Platform::StreamPrototype& self,
                                               Platform::OnHeadersCallback closure) {
  return self.set_on_headers(make_shim(closure));
}

// set_on_data_shim can't be represented by the make_shim above, because it
// also constructs a bytes view onto the envoy_data provided.
Platform::StreamPrototype& set_on_data_shim(Platform::StreamPrototype& self,
                                            OnPyBytesDataCallback closure) {
  return self.set_on_data([closure](envoy_data data, bool end_stream) {
    py::gil_scoped_acquire acquire;
    py::bytes bytes = envoy_data_as_py_bytes(data);
    closure(bytes, end_stream);
  });
}

Platform::StreamPrototype& set_on_trailers_shim(Platform::StreamPrototype& self,
                                                Platform::OnTrailersCallback closure) {
  return self.set_on_trailers(make_shim(closure));
}

Platform::StreamPrototype& set_on_error_shim(Platform::StreamPrototype& self,
                                             Platform::OnErrorCallback closure) {
  return self.set_on_error(make_shim(closure));
}

Platform::StreamPrototype& set_on_complete_shim(Platform::StreamPrototype& self,
                                                Platform::OnCompleteCallback closure) {
  return self.set_on_complete(make_shim(closure));
}

Platform::StreamPrototype& set_on_cancel_shim(Platform::StreamPrototype& self,
                                              Platform::OnCancelCallback closure) {
  return self.set_on_cancel(make_shim(closure));
}

} // namespace StreamPrototype
} // namespace Python
} // namespace Envoy
