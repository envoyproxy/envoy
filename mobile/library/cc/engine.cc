#include "engine.h"

#include "library/common/data/utility.h"
#include "library/common/main_interface.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

Engine::Engine(envoy_engine_t engine) : engine_(engine), terminated_(false) {}

Engine::~Engine() {
  if (!terminated_) {
    terminate();
  }
}

// we lazily construct the stream and pulse clients
// because they either require or will require a weak ptr
// which can't be provided from inside of the constructor
// because of how std::enable_shared_from_this works
StreamClientSharedPtr Engine::streamClient() {
  return std::make_shared<StreamClient>(shared_from_this());
}

PulseClientSharedPtr Engine::pulseClient() { return std::make_shared<PulseClient>(); }

std::string Engine::dumpStats() {
  envoy_data data;
  if (dump_stats(engine_, &data) == ENVOY_FAILURE) {
    return "";
  }
  const std::string to_return = Data::Utility::copyToString(data);
  release_envoy_data(data);

  return to_return;
}

envoy_status_t Engine::terminate() {
  if (terminated_) {
    IS_ENVOY_BUG("attempted to double terminate engine");
    return ENVOY_FAILURE;
  }
  envoy_status_t ret = terminate_engine(engine_, /* release */ false);
  terminated_ = true;
  return ret;
}

} // namespace Platform
} // namespace Envoy
