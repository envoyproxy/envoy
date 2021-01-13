#include "engine.h"

#include "library/common/main_interface.h"
#include "library/common/types/c_types.h"

namespace Envoy {
namespace Platform {

Engine::Engine(envoy_engine_t engine, const std::string& configuration, LogLevel log_level,
               std::function<void()> on_engine_running)
    : engine_(engine), on_engine_running_(on_engine_running) {
  envoy_engine_callbacks callbacks{
      .on_engine_running = &Engine::c_on_engine_running,
      .on_exit = &Engine::c_on_exit,
      .context = this,
  };

  run_engine(this->engine_, callbacks, configuration.c_str(),
             log_level_to_string(log_level).c_str());

  this->stream_client_ = std::make_shared<StreamClient>(this->engine_);
  this->pulse_client_ = std::make_shared<PulseClient>();
}

Engine::~Engine() { terminate_engine(this->engine_); }

StreamClientSharedPtr Engine::stream_client() { return this->stream_client_; }
PulseClientSharedPtr Engine::pulse_client() { return this->pulse_client_; }

void Engine::c_on_engine_running(void* context) {
  Engine* engine = static_cast<Engine*>(context);
  engine->on_engine_running_();
}

void Engine::c_on_exit(void* context) {
  // NOTE: this function is intentionally empty
  // as we don't actually do any post-processing on exit.
  (void)context;
}

} // namespace Platform
} // namespace Envoy
