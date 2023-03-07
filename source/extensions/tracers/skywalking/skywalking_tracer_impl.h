#pragma once

#include "envoy/config/trace/v3/skywalking.pb.h"
#include "envoy/server/tracer_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/trace_driver.h"

#include "source/extensions/tracers/skywalking/tracer.h"
#include "source/tracing_context_impl.h"

#include "cpp2sky/exception.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

using cpp2sky::TracerConfig;
using cpp2sky::TracingContextFactory;
using cpp2sky::TracingContextPtr;

class Driver : public Tracing::Driver, public Logger::Loggable<Logger::Id::tracing> {
public:
  explicit Driver(const envoy::config::trace::v3::SkyWalkingConfig& config,
                  Server::Configuration::TracerFactoryContext& context);

  Tracing::SpanPtr startSpan(const Tracing::Config& config, Tracing::TraceContext& trace_context,
                             const std::string& operation, Envoy::SystemTime start_time,
                             const Tracing::Decision decision) override;

private:
  void loadConfig(const envoy::config::trace::v3::ClientConfig& client_config,
                  Server::Configuration::ServerFactoryContext& server_factory_context);

  class TlsTracer : public ThreadLocal::ThreadLocalObject {
  public:
    TlsTracer(TracerPtr tracer);

    Tracer& tracer();

  private:
    TracerPtr tracer_;
  };

  TracerConfig config_;
  SkyWalkingTracerStatsSharedPtr tracing_stats_;
  ThreadLocal::SlotPtr tls_slot_ptr_;
  std::unique_ptr<TracingContextFactory> tracing_context_factory_;
};

using DriverPtr = std::unique_ptr<Driver>;

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
