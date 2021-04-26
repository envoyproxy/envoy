#include "engine.h"

#include "library/common/main_interface.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

Engine::Engine(envoy_engine_t engine, const std::string& configuration, LogLevel log_level)
    : engine_(engine), terminated_(false) {

  run_engine(this->engine_, configuration.c_str(), log_level_to_string(log_level).c_str());

  this->stream_client_ = std::make_shared<StreamClient>(this->engine_);
  this->pulse_client_ = std::make_shared<PulseClient>();
}

Engine::~Engine() {
  if (!this->terminated_) {
    terminate_engine(this->engine_);
  }
}

StreamClientSharedPtr Engine::stream_client() { return this->stream_client_; }
PulseClientSharedPtr Engine::pulse_client() { return this->pulse_client_; }

void Engine::terminate() {
  if (this->terminated_) {
    throw std::runtime_error("attempting to double terminate Engine");
  }
  terminate_engine(this->engine_);
  this->terminated_ = true;
}

} // namespace Platform
} // namespace Envoy
