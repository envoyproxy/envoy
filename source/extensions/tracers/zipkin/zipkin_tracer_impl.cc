#include "source/extensions/tracers/zipkin/zipkin_tracer_impl.h"

#include "envoy/config/trace/v3/zipkin.pb.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/config/utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/extensions/tracers/zipkin/span_context_extractor.h"
#include "source/extensions/tracers/zipkin/zipkin_core_constants.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

namespace {
// Helper function to parse URI and extract hostname and path
std::pair<std::string, std::string> parseUri(absl::string_view uri) {
  // Find the scheme separator
  size_t scheme_pos = uri.find("://");
  if (scheme_pos == std::string::npos) {
    // No scheme, treat as path only
    return {"", std::string(uri)};
  }

  // Skip past the scheme
  size_t host_start = scheme_pos + 3;

  // Find the path separator
  size_t path_pos = uri.find('/', host_start);
  if (path_pos == std::string::npos) {
    // No path, hostname only
    return {std::string(uri.substr(host_start)), "/"};
  }

  std::string hostname = std::string(uri.substr(host_start, path_pos - host_start));
  std::string path = std::string(uri.substr(path_pos));

  return {hostname, path};
}
} // namespace

Driver::TlsTracer::TlsTracer(TracerPtr&& tracer, Driver& driver)
    : tracer_(std::move(tracer)), driver_(driver) {}

Driver::Driver(const envoy::config::trace::v3::ZipkinConfig& zipkin_config,
               Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
               ThreadLocal::SlotAllocator& tls, Runtime::Loader& runtime,
               const LocalInfo::LocalInfo& local_info, Random::RandomGenerator& random_generator,
               TimeSource& time_source)
    : cm_(cluster_manager),
      tracer_stats_{ZIPKIN_TRACER_STATS(POOL_COUNTER_PREFIX(scope, "tracing.zipkin."))},
      tls_(tls.allocateSlot()), runtime_(runtime), local_info_(local_info),
      time_source_(time_source), trace_context_option_(zipkin_config.trace_context_option()) {
  CollectorInfo collector;

  // Validate that either collector_cluster or collector_service is specified
  if (!zipkin_config.has_collector_service() && zipkin_config.collector_cluster().empty()) {
    throw EnvoyException("Either collector_cluster or collector_service must be specified");
  }

  // Check if HttpService is configured (preferred over legacy fields)
  if (zipkin_config.has_collector_service()) {
    const auto& http_service = zipkin_config.collector_service();
    collector.http_service_ = http_service;

    // Extract cluster and endpoint from HttpService
    const auto& http_uri = http_service.http_uri();

    cluster_ = http_uri.cluster();

    // Parse the URI to extract hostname and path
    auto [parsed_hostname, parsed_path] = parseUri(http_uri.uri());

    if (!parsed_hostname.empty()) {
      // Use the hostname from the URI
      hostname_ = parsed_hostname;
      collector.hostname_ = parsed_hostname;
    } else {
      // Fallback to cluster name if no hostname in URI
      hostname_ = cluster_;
      collector.hostname_ = cluster_;
    }

    // Use the parsed path as the endpoint
    collector.endpoint_ = parsed_path;

    // Parse headers from HttpService
    for (const auto& header_option : http_service.request_headers_to_add()) {
      const auto& header_value = header_option.header();
      collector.request_headers_.emplace_back(Http::LowerCaseString(header_value.key()),
                                              header_value.value());
    }
  } else {

    // Validate required legacy fields
    if (zipkin_config.collector_cluster().empty()) {
      throw EnvoyException("collector_cluster must be specified when not using collector_service");
    }
    if (zipkin_config.collector_endpoint().empty()) {
      throw EnvoyException("collector_endpoint must be specified when using collector_cluster");
    }

    cluster_ = zipkin_config.collector_cluster();
    hostname_ = !zipkin_config.collector_hostname().empty() ? zipkin_config.collector_hostname()
                                                            : zipkin_config.collector_cluster();
    collector.hostname_ = hostname_; // Store hostname in collector as well

    if (!zipkin_config.collector_endpoint().empty()) {
      collector.endpoint_ = zipkin_config.collector_endpoint();
    }

    // Legacy configuration has no custom headers support
    // Custom headers are only available through HttpService
  }

  // Validate cluster exists
  THROW_IF_NOT_OK_REF(Config::Utility::checkCluster("envoy.tracers.zipkin", cluster_, cm_,
                                                    /* allow_added_via_api */ true)
                          .status());

  // The current default version of collector_endpoint_version is HTTP_JSON.
  collector.version_ = zipkin_config.collector_endpoint_version();
  const bool trace_id_128bit = zipkin_config.trace_id_128bit();

  const bool shared_span_context = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      zipkin_config, shared_span_context, DEFAULT_SHARED_SPAN_CONTEXT);
  collector.shared_span_context_ = shared_span_context;

  const bool split_spans_for_request = zipkin_config.split_spans_for_request();

  tls_->set([this, collector, &random_generator, trace_id_128bit, shared_span_context,
             split_spans_for_request](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    TracerPtr tracer = std::make_unique<Tracer>(
        local_info_.clusterName(), local_info_.address(), random_generator, trace_id_128bit,
        shared_span_context, time_source_, split_spans_for_request);
    tracer->setTraceContextOption(trace_context_option_);
    tracer->setReporter(
        ReporterImpl::newInstance(std::ref(*this), std::ref(dispatcher), collector));
    return std::make_shared<TlsTracer>(std::move(tracer), *this);
  });
}

Tracing::SpanPtr Driver::startSpan(const Tracing::Config& config,
                                   Tracing::TraceContext& trace_context,
                                   const StreamInfo::StreamInfo& stream_info, const std::string&,
                                   Tracing::Decision tracing_decision) {
  Tracer& tracer = *tls_->getTyped<TlsTracer>().tracer_;
  SpanPtr new_zipkin_span;

  // W3C fallback extraction is only enabled when USE_B3_WITH_W3C_PROPAGATION is configured
  SpanContextExtractor extractor(trace_context, w3cFallbackEnabled());
  const absl::optional<bool> sampled = extractor.extractSampled();
  bool use_local_decision = !sampled.has_value();
  TRY_NEEDS_AUDIT {
    auto ret_span_context = extractor.extractSpanContext(sampled.value_or(tracing_decision.traced));
    if (!ret_span_context.second) {
      // Create a root Zipkin span. No context was found in the headers.
      new_zipkin_span =
          tracer.startSpan(config, std::string(trace_context.host()), stream_info.startTime());
      new_zipkin_span->setSampled(sampled.value_or(tracing_decision.traced));
    } else {
      use_local_decision = false;
      new_zipkin_span = tracer.startSpan(config, std::string(trace_context.host()),
                                         stream_info.startTime(), ret_span_context.first);
    }
  }
  END_TRY catch (const ExtractorException& e) { return std::make_unique<Tracing::NullSpan>(); }

  new_zipkin_span->setUseLocalDecision(use_local_decision);
  // Return the active Zipkin span.
  return new_zipkin_span;
}

ReporterImpl::ReporterImpl(Driver& driver, Event::Dispatcher& dispatcher,
                           const CollectorInfo& collector)
    : driver_(driver), collector_(collector),
      span_buffer_{
          std::make_unique<SpanBuffer>(collector.version_, collector.shared_span_context_)},
      collector_cluster_(driver_.clusterManager(), driver_.cluster()) {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    driver_.tracerStats().timer_flushed_.inc();
    flushSpans();
    enableTimer();
  });

  const uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);
  span_buffer_->allocateBuffer(min_flush_spans);

  enableTimer();
}

ReporterPtr ReporterImpl::newInstance(Driver& driver, Event::Dispatcher& dispatcher,
                                      const CollectorInfo& collector) {
  return std::make_unique<ReporterImpl>(driver, dispatcher, collector);
}

void ReporterImpl::reportSpan(Span&& span) {
  span_buffer_->addSpan(std::move(span));

  const uint64_t min_flush_spans =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);

  if (span_buffer_->pendingSpans() == min_flush_spans) {
    flushSpans();
  }
}

void ReporterImpl::enableTimer() {
  const uint64_t flush_interval =
      driver_.runtime().snapshot().getInteger("tracing.zipkin.flush_interval_ms", 5000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void ReporterImpl::flushSpans() {
  if (span_buffer_->pendingSpans()) {
    driver_.tracerStats().spans_sent_.add(span_buffer_->pendingSpans());
    const std::string request_body = span_buffer_->serialize();
    Http::RequestMessagePtr message = std::make_unique<Http::RequestMessageImpl>();
    message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
    // Set path and hostname - both are stored in collector_
    message->headers().setPath(collector_.endpoint_);
    message->headers().setHost(collector_.hostname_);

    message->headers().setReferenceContentType(
        collector_.version_ == envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO
            ? Http::Headers::get().ContentTypeValues.Protobuf
            : Http::Headers::get().ContentTypeValues.Json);

    // Add custom headers from collector configuration
    for (const auto& header : collector_.request_headers_) {
      // Replace any existing header with the configured value
      message->headers().setCopy(header.first, header.second);
    }

    message->body().add(request_body);

    const uint64_t timeout =
        driver_.runtime().snapshot().getInteger("tracing.zipkin.request_timeout", 5000U);

    if (collector_cluster_.threadLocalCluster().has_value()) {
      Http::AsyncClient::Request* request =
          collector_cluster_.threadLocalCluster()->get().httpAsyncClient().send(
              std::move(message), *this,
              Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(timeout)));
      if (request) {
        active_requests_.add(*request);
      }
    } else {
      ENVOY_LOG(debug, "collector cluster '{}' does not exist", driver_.cluster());
      driver_.tracerStats().reports_skipped_no_cluster_.inc();
    }

    span_buffer_->clear();
  }
}

void ReporterImpl::onFailure(const Http::AsyncClient::Request& request,
                             Http::AsyncClient::FailureReason) {
  active_requests_.remove(request);
  driver_.tracerStats().reports_failed_.inc();
}

void ReporterImpl::onSuccess(const Http::AsyncClient::Request& request,
                             Http::ResponseMessagePtr&& http_response) {
  active_requests_.remove(request);
  if (Http::Utility::getResponseStatus(http_response->headers()) !=
      enumToInt(Http::Code::Accepted)) {
    driver_.tracerStats().reports_dropped_.inc();
  } else {
    driver_.tracerStats().reports_sent_.inc();
  }
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
