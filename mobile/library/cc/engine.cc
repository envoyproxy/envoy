#include "engine.h"

#include "library/common/internal_engine.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

Engine::Engine(::Envoy::InternalEngine* engine) : engine_(engine) {}

Engine::~Engine() {
  if (!engine_->isTerminated()) {
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

std::string Engine::dumpStats() { return engine_->dumpStats(); }

envoy_status_t Engine::terminate() { return engine_->terminate(); }

} // namespace Platform
} // namespace Envoy
