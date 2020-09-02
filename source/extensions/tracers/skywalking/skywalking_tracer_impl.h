#pragma once

#include "envoy/config/trace/v3/skywalking.pb.h"
#include "envoy/server/tracer_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"

#include "common/tracing/http_tracer_impl.h"

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
                             const std::string& operation_name, Envoy::SystemTime start_time,
                             const Tracing::Decision tracing_decision) override;

private:
  struct TlsTracer : ThreadLocal::ThreadLocalObject {
    TlsTracer(TracerPtr tracer, Driver& driver) : tracer_(std::move(tracer)), driver_(driver) {}

    TracerPtr tracer_;
    Driver& driver_;
  };

  ThreadLocal::SlotPtr tls_slot_ptr_;
};

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
