#include "extensions/tracers/lightstep/lightstep_tracer_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/trace/v3/lightstep.pb.h"

#include "common/buffer/buffer_impl.h"
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

static Buffer::InstancePtr serializeGrpcMessage(const lightstep::BufferChain& buffer_chain) {
  Buffer::InstancePtr body(new Buffer::OwnedImpl());
  auto size = buffer_chain.num_bytes();
  Buffer::RawSlice iovec;
  body->reserve(size, &iovec, 1);
  ASSERT(iovec.len_ >= size);
  iovec.len_ = size;
  buffer_chain.CopyOut(static_cast<char*>(iovec.mem_), size);
  body->commit(&iovec, 1);
  Grpc::Common::prependGrpcFrameHeader(*body);
  return body;
}

static std::vector<lightstep::PropagationMode>
MakePropagationModes(const envoy::config::trace::v3::LightstepConfig& lightstep_config) {
  if (lightstep_config.propagation_modes().empty()) {
    return {lightstep::PropagationMode::envoy};
  }
  std::vector<lightstep::PropagationMode> result;
  result.reserve(lightstep_config.propagation_modes().size());
  for (auto propagation_mode : lightstep_config.propagation_modes()) {
    switch (propagation_mode) {
    case envoy::config::trace::v3::LightstepConfig::ENVOY:
      result.push_back(lightstep::PropagationMode::envoy);
      break;
    case envoy::config::trace::v3::LightstepConfig::LIGHTSTEP:
      result.push_back(lightstep::PropagationMode::lightstep);
      break;
    case envoy::config::trace::v3::LightstepConfig::B3:
      result.push_back(lightstep::PropagationMode::b3);
      break;
    case envoy::config::trace::v3::LightstepConfig::TRACE_CONTEXT:
      result.push_back(lightstep::PropagationMode::trace_context);
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  return result;
}

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

// If the default min_flush_spans value is too small, the larger number of reports can overwhelm
// LightStep's satellites. Hence, we need to choose a number that's large enough; though, it's
// somewhat arbitrary.
//
// See https://github.com/lightstep/lightstep-tracer-cpp/issues/106
const size_t LightStepDriver::DefaultMinFlushSpans = 200U;

LightStepDriver::LightStepTransporter::LightStepTransporter(LightStepDriver& driver)
    : driver_(driver), collector_cluster_(driver_.clusterManager(), driver_.cluster()) {}

LightStepDriver::LightStepTransporter::~LightStepTransporter() {
  if (active_request_ != nullptr) {
    active_request_->cancel();
  }
}

void LightStepDriver::LightStepTransporter::onSuccess(const Http::AsyncClient::Request&,
                                                      Http::ResponseMessagePtr&& /*response*/) {
  driver_.grpc_context_.chargeStat(*active_cluster_, driver_.request_stat_names_, true);
  active_callback_->OnSuccess(*active_report_);
  reset();
}

void LightStepDriver::LightStepTransporter::onFailure(
    const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason /*failure_reason*/) {
  driver_.grpc_context_.chargeStat(*active_cluster_, driver_.request_stat_names_, false);
  active_callback_->OnFailure(*active_report_);
  reset();
}

void LightStepDriver::LightStepTransporter::OnSpanBufferFull() noexcept {
  if (active_report_ != nullptr) {
    return;
  }
  driver_.flush();
}

void LightStepDriver::LightStepTransporter::Send(std::unique_ptr<lightstep::BufferChain>&& report,
                                                 Callback& callback) noexcept {
  if (active_report_ != nullptr) {
    callback.OnFailure(*report);
    return;
  }

  const uint64_t timeout =
      driver_.runtime().snapshot().getInteger("tracing.lightstep.request_timeout", 5000U);
  Http::RequestMessagePtr message = Grpc::Common::prepareHeaders(
      driver_.cluster(), lightstep::CollectorServiceFullName(), lightstep::CollectorMethodName(),
      absl::optional<std::chrono::milliseconds>(timeout));
  message->body() = serializeGrpcMessage(*report);

  if (collector_cluster_.exists()) {
    active_report_ = std::move(report);
    active_callback_ = &callback;
    active_cluster_ = collector_cluster_.info();
    active_request_ = driver_.clusterManager()
                          .httpAsyncClientForCluster(collector_cluster_.info()->name())
                          .send(std::move(message), *this,
                                Http::AsyncClient::RequestOptions().setTimeout(
                                    std::chrono::milliseconds(timeout)));
  } else {
    ENVOY_LOG(debug, "collector cluster '{}' does not exist", driver_.cluster());
    driver_.tracerStats().reports_skipped_no_cluster_.inc();
  }
}

void LightStepDriver::LightStepTransporter::reset() {
  active_cluster_ = nullptr;
  active_request_ = nullptr;
  active_callback_ = nullptr;
  active_report_ = nullptr;
}

LightStepDriver::LightStepMetricsObserver::LightStepMetricsObserver(LightStepDriver& driver)
    : driver_(driver) {}

void LightStepDriver::LightStepMetricsObserver::OnSpansSent(int num_spans) noexcept {
  driver_.tracerStats().spans_sent_.add(num_spans);
}

void LightStepDriver::LightStepMetricsObserver::OnSpansDropped(int num_spans) noexcept {
  driver_.tracerStats().spans_dropped_.add(num_spans);
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

lightstep::LightStepTracer& LightStepDriver::TlsLightStepTracer::tracer() { return *tracer_; }

void LightStepDriver::TlsLightStepTracer::enableTimer() {
  const uint64_t flush_interval =
      driver_.runtime().snapshot().getInteger("tracing.lightstep.flush_interval_ms", 1000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

LightStepDriver::LightStepDriver(const envoy::config::trace::v3::LightstepConfig& lightstep_config,
                                 Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                                 ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
                                 std::unique_ptr<lightstep::LightStepTracerOptions>&& options,
                                 PropagationMode propagation_mode, Grpc::Context& grpc_context)
    : OpenTracingDriver{scope}, cm_{cluster_manager},
      tracer_stats_{LIGHTSTEP_TRACER_STATS(POOL_COUNTER_PREFIX(scope, "tracing.lightstep."))},
      tls_{tls.allocateSlot()}, runtime_{runtime}, options_{std::move(options)},
      propagation_mode_{propagation_mode}, grpc_context_(grpc_context),
      pool_(scope.symbolTable()), request_stat_names_{
                                      pool_.add(lightstep::CollectorServiceFullName()),
                                      pool_.add(lightstep::CollectorMethodName())} {

  Config::Utility::checkCluster(TracerNames::get().Lightstep, lightstep_config.collector_cluster(),
                                cm_, /* allow_added_via_api */ true);
  cluster_ = lightstep_config.collector_cluster();

  if (!(cm_.get(cluster_)->info()->features() & Upstream::ClusterInfo::Features::HTTP2)) {
    throw EnvoyException(
        fmt::format("{} collector cluster must support http2 for gRPC calls", cluster_));
  }

  auto propagation_modes = MakePropagationModes(lightstep_config);

  tls_->set([this, propagation_modes = std::move(propagation_modes)](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    lightstep::LightStepTracerOptions tls_options;
    tls_options.access_token = options_->access_token;
    tls_options.component_name = options_->component_name;
    tls_options.use_thread = false;
    tls_options.propagation_modes = propagation_modes;
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

void LightStepDriver::flush() {
  auto& tls_tracer = tls_->getTyped<TlsLightStepTracer>();
  tls_tracer.tracer().Flush();
  tls_tracer.enableTimer();
}

opentracing::Tracer& LightStepDriver::tracer() {
  return tls_->getTyped<TlsLightStepTracer>().tracer();
}

} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
