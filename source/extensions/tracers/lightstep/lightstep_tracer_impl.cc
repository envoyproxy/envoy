#include "extensions/tracers/lightstep/lightstep_tracer_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/common/base64.h"
#include "common/common/fmt.h"
#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/http/message_impl.h"
#include "common/tracing/http_tracer_impl.h"

#include "extensions/tracers/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {

void LightStepLogger::operator()(lightstep::LogLevel level,
                                 opentracing::string_view message) const {
  const fmt::string_view fmt_message{message.data(), message.size()};
  switch (level) {
  case lightstep::LogLevel::debug:
    ENVOY_LOG(debug, "{}", fmt_message);
    break;
  case lightstep::LogLevel::info:
    ENVOY_LOG(info, "{}", fmt_message);
    break;
  default:
    ENVOY_LOG(warn, "{}", fmt_message);
    break;
  }
}

LightStepDriver::LightStepTransporter::LightStepTransporter(LightStepDriver& driver)
    : driver_(driver) {}

// If the default min_flush_spans value is too small, the larger number of reports can overwhelm
// LightStep's satellites. Hence, we need to choose a number that's large enough; though, it's
// somewhat arbitrary.
//
// See https://github.com/lightstep/lightstep-tracer-cpp/issues/106
const size_t LightStepDriver::DefaultMinFlushSpans = 200U;

LightStepDriver::LightStepTransporter::~LightStepTransporter() {
  if (active_request_ != nullptr) {
    active_request_->cancel();
  }
}

void LightStepDriver::LightStepTransporter::Send(const Protobuf::Message& request,
                                                 Protobuf::Message& response,
                                                 lightstep::AsyncTransporter::Callback& callback) {
  // TODO(rnburn): Update to use Grpc::AsyncClient when it supports abstract message classes.
  active_callback_ = &callback;
  active_response_ = &response;

  const uint64_t timeout =
      driver_.runtime().snapshot().getInteger("tracing.lightstep.request_timeout", 5000U);
  Http::MessagePtr message = Grpc::Common::prepareHeaders(
      driver_.cluster()->name(), lightstep::CollectorServiceFullName(),
      lightstep::CollectorMethodName(), absl::optional<std::chrono::milliseconds>(timeout));
  message->body() = Grpc::Common::serializeBody(request);

  active_request_ =
      driver_.clusterManager()
          .httpAsyncClientForCluster(driver_.cluster()->name())
          .send(std::move(message), *this,
                Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(timeout)));
}

void LightStepDriver::LightStepTransporter::onSuccess(Http::MessagePtr&& response) {
  try {
    active_request_ = nullptr;
    Grpc::Common::validateResponse(*response);

    // http://www.grpc.io/docs/guides/wire.html
    // First 5 bytes contain the message header.
    response->body()->drain(5);
    Buffer::ZeroCopyInputStreamImpl stream{std::move(response->body())};
    if (!active_response_->ParseFromZeroCopyStream(&stream)) {
      throw EnvoyException("Failed to parse LightStep collector response");
    }
    Grpc::Common::chargeStat(*driver_.cluster(), lightstep::CollectorServiceFullName(),
                             lightstep::CollectorMethodName(), true);
    active_callback_->OnSuccess();
  } catch (const Grpc::Exception& ex) {
    Grpc::Common::chargeStat(*driver_.cluster(), lightstep::CollectorServiceFullName(),
                             lightstep::CollectorMethodName(), false);
    active_callback_->OnFailure(std::make_error_code(std::errc::network_down));
  } catch (const EnvoyException& ex) {
    Grpc::Common::chargeStat(*driver_.cluster(), lightstep::CollectorServiceFullName(),
                             lightstep::CollectorMethodName(), false);
    active_callback_->OnFailure(std::make_error_code(std::errc::bad_message));
  }
}

void LightStepDriver::LightStepTransporter::onFailure(Http::AsyncClient::FailureReason) {
  active_request_ = nullptr;
  Grpc::Common::chargeStat(*driver_.cluster(), lightstep::CollectorServiceFullName(),
                           lightstep::CollectorMethodName(), false);
  active_callback_->OnFailure(std::make_error_code(std::errc::network_down));
}

LightStepDriver::LightStepMetricsObserver::LightStepMetricsObserver(LightStepDriver& driver)
    : driver_(driver) {}

void LightStepDriver::LightStepMetricsObserver::OnSpansSent(int num_spans) {
  driver_.tracerStats().spans_sent_.add(num_spans);
}

LightStepDriver::TlsLightStepTracer::TlsLightStepTracer(
    const std::shared_ptr<lightstep::LightStepTracer>& tracer, LightStepDriver& driver,
    Event::Dispatcher& dispatcher)
    : tracer_{tracer}, driver_{driver} {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    driver_.tracerStats().timer_flushed_.inc();
    tracer_->Flush();
    enableTimer();
  });

  enableTimer();
}

opentracing::Tracer& LightStepDriver::TlsLightStepTracer::tracer() { return *tracer_; }

void LightStepDriver::TlsLightStepTracer::enableTimer() {
  const uint64_t flush_interval =
      driver_.runtime().snapshot().getInteger("tracing.lightstep.flush_interval_ms", 1000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

LightStepDriver::LightStepDriver(const envoy::config::trace::v2::LightstepConfig& lightstep_config,
                                 Upstream::ClusterManager& cluster_manager, Stats::Store& stats,
                                 ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
                                 std::unique_ptr<lightstep::LightStepTracerOptions>&& options,
                                 PropagationMode propagation_mode)
    : OpenTracingDriver{stats}, cm_{cluster_manager},
      tracer_stats_{LIGHTSTEP_TRACER_STATS(POOL_COUNTER_PREFIX(stats, "tracing.lightstep."))},
      tls_{tls.allocateSlot()}, runtime_{runtime}, options_{std::move(options)},
      propagation_mode_{propagation_mode} {
  Config::Utility::checkCluster(TracerNames::get().Lightstep, lightstep_config.collector_cluster(),
                                cm_);
  cluster_ = cm_.get(lightstep_config.collector_cluster())->info();

  if (!(cluster_->features() & Upstream::ClusterInfo::Features::HTTP2)) {
    throw EnvoyException(
        fmt::format("{} collector cluster must support http2 for gRPC calls", cluster_->name()));
  }

  tls_->set([this](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    lightstep::LightStepTracerOptions tls_options;
    tls_options.access_token = options_->access_token;
    tls_options.component_name = options_->component_name;
    tls_options.use_thread = false;
    tls_options.use_single_key_propagation = true;
    tls_options.logger_sink = LightStepLogger{};

    tls_options.max_buffered_spans = std::function<size_t()>{[this] {
      return runtime_.snapshot().getInteger("tracing.lightstep.min_flush_spans",
                                            DefaultMinFlushSpans);
    }};
    tls_options.metrics_observer = std::make_unique<LightStepMetricsObserver>(*this);
    tls_options.transporter = std::make_unique<LightStepTransporter>(*this);
    std::shared_ptr<lightstep::LightStepTracer> tracer =
        lightstep::MakeLightStepTracer(std::move(tls_options));

    return ThreadLocal::ThreadLocalObjectSharedPtr{
        new TlsLightStepTracer{tracer, *this, dispatcher}};
  });
}

opentracing::Tracer& LightStepDriver::tracer() {
  return tls_->getTyped<TlsLightStepTracer>().tracer();
}

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
