#include "source/extensions/tracers/zipkin/zipkin_tracer_impl.h"

#include <memory>

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

Driver::Driver(const envoy::config::trace::v3::ZipkinConfig& zipkin_config,
               Server::Configuration::ServerFactoryContext& context)
    : collector_(std::make_shared<CollectorInfo>()), tls_(context.threadLocal().allocateSlot()),
      trace_context_option_(zipkin_config.trace_context_option()) {

  // Check if HttpService is configured (preferred over legacy fields)
  if (zipkin_config.has_collector_service()) {
    const auto& http_service = zipkin_config.collector_service();
    // Extract cluster and endpoint from HttpService
    const auto& http_uri = http_service.http_uri();

    collector_->cluster_ = http_uri.cluster();

    // Parse the URI to extract hostname and path
    if (auto [hostname, path] = parseUri(http_uri.uri()); !hostname.empty()) {
      // Use the hostname from the URI
      collector_->hostname_ = hostname;
      collector_->endpoint_ = path;
    } else {
      // Fallback to cluster name if no hostname in URI
      collector_->hostname_ = collector_->cluster_;
      collector_->endpoint_ = path;
    }

    // Parse headers from HttpService
    for (const auto& header_option : http_service.request_headers_to_add()) {
      const auto& header_value = header_option.header();
      collector_->request_headers_.emplace_back(header_value.key(), header_value.value());
    }
  } else {
    if (zipkin_config.collector_cluster().empty() || zipkin_config.collector_endpoint().empty()) {
      throw EnvoyException(
          "collector_cluster and collector_endpoint must be specified when not using "
          "collector_service");
    }

    collector_->cluster_ = zipkin_config.collector_cluster();
    collector_->hostname_ = !zipkin_config.collector_hostname().empty()
                                ? zipkin_config.collector_hostname()
                                : zipkin_config.collector_cluster();
    collector_->endpoint_ = zipkin_config.collector_endpoint();

    // Legacy configuration has no custom headers support
    // Custom headers are only available through HttpService.
  }

  // Validate cluster exists
  THROW_IF_NOT_OK_REF(Config::Utility::checkCluster("envoy.tracers.zipkin", collector_->cluster_,
                                                    context.clusterManager(),
                                                    /* allow_added_via_api */ true)
                          .status());

  // The current default version of collector_endpoint_version is HTTP_JSON.
  collector_->version_ = zipkin_config.collector_endpoint_version();
  const bool trace_id_128bit = zipkin_config.trace_id_128bit();

  const bool shared_span_context = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
      zipkin_config, shared_span_context, DEFAULT_SHARED_SPAN_CONTEXT);
  collector_->shared_span_context_ = shared_span_context;

  const bool split_spans_for_request = zipkin_config.split_spans_for_request();

  auto stats = std::make_shared<ZipkinTracerStats>(ZipkinTracerStats{
      ZIPKIN_TRACER_STATS(POOL_COUNTER_PREFIX(context.scope(), "tracing.zipkin."))});

  tls_->set([&context, c = collector_, t = trace_context_option_, stats, trace_id_128bit,
             split_spans_for_request](
                Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    TracerPtr tracer = std::make_unique<Tracer>(
        context.localInfo().clusterName(), context.localInfo().address(),
        context.api().randomGenerator(), trace_id_128bit, c->shared_span_context_,
        context.timeSource(), split_spans_for_request);
    tracer->setTraceContextOption(t);
    auto reporter = std::make_unique<ReporterImpl>(dispatcher, context.clusterManager(),
                                                   context.runtime(), stats, c);
    tracer->setReporter(std::move(reporter));
    return std::make_shared<Driver::TlsTracer>(std::move(tracer));
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

ReporterImpl::ReporterImpl(Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm,
                           Runtime::Loader& runtime, ZipkinTracerStatsSharedPtr tracer_stats,
                           CollectorInfoConstSharedPtr collector)
    : runtime_(runtime), tracer_stats_(std::move(tracer_stats)), collector_(std::move(collector)),
      span_buffer_{
          std::make_unique<SpanBuffer>(collector_->version_, collector_->shared_span_context_)},
      collector_cluster_(cm, collector_->cluster_) {
  flush_timer_ = dispatcher.createTimer([this]() -> void {
    tracer_stats_->timer_flushed_.inc();
    flushSpans();
    enableTimer();
  });

  const uint64_t min_flush_spans =
      runtime_.snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);
  span_buffer_->allocateBuffer(min_flush_spans);

  enableTimer();
}

void ReporterImpl::reportSpan(Span&& span) {
  span_buffer_->addSpan(std::move(span));

  const uint64_t min_flush_spans =
      runtime_.snapshot().getInteger("tracing.zipkin.min_flush_spans", 5U);

  if (span_buffer_->pendingSpans() == min_flush_spans) {
    flushSpans();
  }
}

void ReporterImpl::enableTimer() {
  const uint64_t flush_interval =
      runtime_.snapshot().getInteger("tracing.zipkin.flush_interval_ms", 5000U);
  flush_timer_->enableTimer(std::chrono::milliseconds(flush_interval));
}

void ReporterImpl::flushSpans() {
  if (span_buffer_->pendingSpans()) {
    tracer_stats_->spans_sent_.add(span_buffer_->pendingSpans());
    const std::string request_body = span_buffer_->serialize();
    Http::RequestMessagePtr message = std::make_unique<Http::RequestMessageImpl>();
    message->headers().setReferenceMethod(Http::Headers::get().MethodValues.Post);
    // Set path and hostname - both are stored in collector_
    message->headers().setPath(collector_->endpoint_);
    message->headers().setHost(collector_->hostname_);

    message->headers().setReferenceContentType(
        collector_->version_ == envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO
            ? Http::Headers::get().ContentTypeValues.Protobuf
            : Http::Headers::get().ContentTypeValues.Json);

    // Add custom headers from collector configuration
    for (const auto& header : collector_->request_headers_) {
      // Replace any existing header with the configured value
      message->headers().setCopy(header.first, header.second);
    }

    message->body().add(request_body);

    const uint64_t timeout =
        runtime_.snapshot().getInteger("tracing.zipkin.request_timeout", 5000U);

    if (collector_cluster_.threadLocalCluster().has_value()) {
      Http::AsyncClient::Request* request =
          collector_cluster_.threadLocalCluster()->get().httpAsyncClient().send(
              std::move(message), *this,
              Http::AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(timeout)));
      if (request) {
        active_requests_.add(*request);
      }
    } else {
      ENVOY_LOG(debug, "collector cluster '{}' does not exist", collector_->cluster_);
      tracer_stats_->reports_skipped_no_cluster_.inc();
    }

    span_buffer_->clear();
  }
}

void ReporterImpl::onFailure(const Http::AsyncClient::Request& request,
                             Http::AsyncClient::FailureReason) {
  active_requests_.remove(request);
  tracer_stats_->reports_failed_.inc();
}

void ReporterImpl::onSuccess(const Http::AsyncClient::Request& request,
                             Http::ResponseMessagePtr&& http_response) {
  active_requests_.remove(request);
  if (Http::Utility::getResponseStatus(http_response->headers()) !=
      enumToInt(Http::Code::Accepted)) {
    tracer_stats_->reports_dropped_.inc();
  } else {
    tracer_stats_->reports_sent_.inc();
  }
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
