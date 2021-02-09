#include "engine.h"

#include "library/common/main_interface.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

namespace {

void c_on_engine_running(void* context) {
  EngineCallbacks* engine_callbacks = static_cast<EngineCallbacks*>(context);
  engine_callbacks->on_engine_running();
}

void c_on_exit(void* context) {
  // NOTE: this function is intentionally empty
  // as we don't actually do any post-processing on exit.
  (void)context;
}

} // namespace

Engine::Engine(envoy_engine_t engine, const std::string& configuration, LogLevel log_level,
               EngineCallbacksSharedPtr callbacks)
    : engine_(engine), callbacks_(callbacks) {
  envoy_engine_callbacks envoy_callbacks{
      .on_engine_running = &c_on_engine_running,
      .on_exit = &c_on_exit,
      .context = &this->callbacks_,
  };

  run_engine(this->engine_, envoy_callbacks, configuration.c_str(),
             log_level_to_string(log_level).c_str());

  this->stream_client_ = std::make_shared<StreamClient>(this->engine_);
  this->pulse_client_ = std::make_shared<PulseClient>();
}

Engine::~Engine() { terminate_engine(this->engine_); }

StreamClientSharedPtr Engine::stream_client() { return this->stream_client_; }
PulseClientSharedPtr Engine::pulse_client() { return this->pulse_client_; }

} // namespace Platform
} // namespace Envoy
