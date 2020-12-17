#pragma once

// NOLINT(namespace-envoy)

#include "common/common/base_logger.h"

#include "executor.h"
#include "library/common/types/c_types.h"
#include "pulse_client.h"
#include "stream_client.h"

class Engine {
public:
  StreamClientSharedPtr stream_client();
  PulseClientSharedPtr pulse_client();

private:
  Engine(envoy_engine_t engine, const std::string& configuration,
         Envoy::Logger::Logger::Levels log_level, std::function<void()> on_engine_running,
         ExecutorSharedPtr executor_);

  friend class EngineBuilder;

  StreamClientSharedPtr stream_client_;
  PulseClientSharedPtr pulse_client_;
  ExecutorSharedPtr executor_;
};

using EngineSharedPtr = std::shared_ptr<Engine>;
