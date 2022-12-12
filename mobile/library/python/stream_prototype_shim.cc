#include "stream_prototype_shim.h"

#include "bytes_view.h"

namespace Envoy {
namespace Python {
namespace StreamPrototype {

template <typename... Ts> std::function<void(Ts...)> makeShim(std::function<void(Ts...)> closure) {
  return [closure](Ts... args) {
    py::gil_scoped_acquire acquire;
    closure(std::forward<Ts>(args)...);
  };
}

Platform::StreamPrototype& setOnHeadersShim(Platform::StreamPrototype& self,
                                            Platform::OnHeadersCallback closure) {
  return self.setOnHeaders(makeShim(closure));
}

// set_on_data_shim can't be represented by the makeShim above, because it
// also constructs a bytes view onto the envoy_data provided.
Platform::StreamPrototype& setOnDataShim(Platform::StreamPrototype& self,
                                         OnPyBytesDataCallback closure) {
  return self.setOnData([closure](envoy_data data, bool end_stream) {
    py::gil_scoped_acquire acquire;
    py::bytes bytes = envoyDataAsPyBytes(data);
    closure(bytes, end_stream);
  });
}

Platform::StreamPrototype& setOnTrailersShim(Platform::StreamPrototype& self,
                                             Platform::OnTrailersCallback closure) {
  return self.setOnTrailers(makeShim(closure));
}

Platform::StreamPrototype& setOnErrorShim(Platform::StreamPrototype& self,
                                          Platform::OnErrorCallback closure) {
  return self.setOnError(makeShim(closure));
}

Platform::StreamPrototype& setOnCompleteShim(Platform::StreamPrototype& self,
                                             Platform::OnCompleteCallback closure) {
  return self.setOnComplete(makeShim(closure));
}

Platform::StreamPrototype& setOnCancelShim(Platform::StreamPrototype& self,
                                           Platform::OnCancelCallback closure) {
  return self.setOnCancel(makeShim(closure));
}

} // namespace StreamPrototype
} // namespace Python
} // namespace Envoy
