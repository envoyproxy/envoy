#include "engine.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "library/common/internal_engine.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

absl::StatusOr<std::shared_ptr<Engine>>
Engine::createFromInternalEngineHandle(int64_t internal_engine_handle) {
  auto* internal_engine = reinterpret_cast<::Envoy::InternalEngine*>(internal_engine_handle);
  if (internal_engine == nullptr) {
    return absl::InvalidArgumentError("Invalid internal engine handle.");
  }
  return std::shared_ptr<Engine>(new Engine(internal_engine, /*handle_termination=*/false));
}

Engine::Engine(::Envoy::InternalEngine* engine, bool handle_termination)
    : engine_(engine), handle_termination_(handle_termination) {}

Engine::~Engine() {
  if (handle_termination_ && !engine_->isTerminated()) {
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

int64_t Engine::getInternalEngineHandle() const { return reinterpret_cast<int64_t>(engine_); }

envoy_status_t Engine::terminate() { return engine_->terminate(); }

void Engine::onDefaultNetworkChangeEvent(int network) {
  engine_->onDefaultNetworkChangeEvent(network);
}

void Engine::onDefaultNetworkChanged(int network) { engine_->onDefaultNetworkChanged(network); }

void Engine::onDefaultNetworkUnavailable() { engine_->onDefaultNetworkUnavailable(); }

void Engine::onDefaultNetworkAvailable() { engine_->onDefaultNetworkAvailable(); }

envoy_status_t Engine::setProxySettings(absl::string_view host, const uint16_t port) {
  return engine_->setProxySettings(host, port);
}

} // namespace Platform
} // namespace Envoy
