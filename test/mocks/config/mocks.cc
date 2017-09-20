#include "test/mocks/config/mocks.h"

namespace Envoy {
namespace Config {

MockGrpcMuxWatch::MockGrpcMuxWatch() {}
MockGrpcMuxWatch::~MockGrpcMuxWatch() { cancel(); }

MockGrpcMux::MockGrpcMux() {}
MockGrpcMux::~MockGrpcMux() {}

GrpcMuxWatchPtr MockGrpcMux::subscribe(const std::string& type_url,
                                       const std::vector<std::string>& resources,
                                       GrpcMuxCallbacks& callbacks) {
  return GrpcMuxWatchPtr(subscribe_(type_url, resources, callbacks));
}

MockGrpcMuxCallbacks::MockGrpcMuxCallbacks() {}
MockGrpcMuxCallbacks::~MockGrpcMuxCallbacks() {}

} // namespace Config
} // namespace Envoy
