#include "engine.h"

#include "absl/strings/string_view.h"
#include "library/common/internal_engine.h"
#include "library/common/types/c_types.h"

#if defined(__APPLE__)
#include "library/cc/apple/apple_network_change_monitor.h"
#endif

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

void Engine::initializeNetworkChangeMonitor() {
#if defined(__APPLE__)
  network_change_monitor_ = std::make_unique<AppleNetworkChangeMonitor>(*this);
#endif
  if (network_change_monitor_ != nullptr) {
    network_change_monitor_->start();
  }
}

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
