#pragma once

#include "envoy/config/trace/v3/skywalking.pb.h"
#include "envoy/server/tracer_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"

#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/skywalking/skywalking_client_config.h"
#include "extensions/tracers/skywalking/tracer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

class Driver : public Tracing::Driver, public Logger::Loggable<Logger::Id::tracing> {
public:
  explicit Driver(const envoy::config::trace::v3::SkyWalkingConfig& config,
                  Server::Configuration::TracerFactoryContext& context);

  Tracing::SpanPtr startSpan(const Tracing::Config& config, Http::RequestHeaderMap& request_headers,
                             const std::string& operation, Envoy::SystemTime start_time,
                             const Tracing::Decision decision) override;

private:
  struct TlsTracer : ThreadLocal::ThreadLocalObject {
    TlsTracer(TracerPtr tracer) : tracer_(std::move(tracer)) {}

    TracerPtr tracer_;
  };

  SkyWalkingTracerStats tracing_stats_;

  SkyWalkingClientConfigPtr client_config_;

  // This random_generator_ will be used to create SkyWalking trace id and segment id.
  Random::RandomGenerator& random_generator_;
  ThreadLocal::SlotPtr tls_slot_ptr_;
};

using DriverPtr = std::unique_ptr<Driver>;

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
