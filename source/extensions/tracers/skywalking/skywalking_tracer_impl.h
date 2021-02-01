#pragma once

#include "envoy/config/trace/v3/skywalking.pb.h"
#include "envoy/server/tracer_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/tracing/http_tracer.h"

#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/skywalking/tracer.h"

#include "cpp2sky/exception.h"
#include "cpp2sky/segment_context.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {

using cpp2sky::SegmentContextFactoryPtr;
using cpp2sky::SegmentContextPtr;
using cpp2sky::TracerConfig;

class Driver : public Tracing::Driver, public Logger::Loggable<Logger::Id::tracing> {
public:
  explicit Driver(const envoy::config::trace::v3::SkyWalkingConfig& config,
                  Server::Configuration::TracerFactoryContext& context);

  Tracing::SpanPtr startSpan(const Tracing::Config& config, Http::RequestHeaderMap& request_headers,
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
  SkyWalkingTracerStats tracing_stats_;
  ThreadLocal::SlotPtr tls_slot_ptr_;
  SegmentContextFactoryPtr segment_context_factory_;
};

using DriverPtr = std::unique_ptr<Driver>;

} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
