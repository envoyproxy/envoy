#include "engine.h"

#include "library/common/data/utility.h"
#include "library/common/engine.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

Engine::Engine(::Envoy::Engine* engine) : engine_(engine), terminated_(false) {}

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

std::string Engine::dumpStats() {
  envoy_data data;
  if (engine_->dumpStats(&data) == ENVOY_FAILURE) {
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
  envoy_status_t ret = engine_->terminate();
  terminated_ = true;
  return ret;
}

} // namespace Platform
} // namespace Envoy
