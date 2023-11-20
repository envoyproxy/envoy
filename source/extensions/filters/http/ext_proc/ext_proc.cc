#include "source/extensions/filters/http/ext_proc/ext_proc.h"

#include "envoy/config/common/mutation_rules/v3/mutation_rules.pb.h"

#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/http/ext_proc/mutation_utils.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using envoy::config::common::mutation_rules::v3::HeaderMutationRules;
using envoy::extensions::filters::http::ext_proc::v3::ExtProcPerRoute;
using envoy::extensions::filters::http::ext_proc::v3::ProcessingMode;
using envoy::type::v3::StatusCode;

using envoy::service::ext_proc::v3::ImmediateResponse;
using envoy::service::ext_proc::v3::ProcessingRequest;
using envoy::service::ext_proc::v3::ProcessingResponse;

using Filters::Common::MutationRules::Checker;
using Http::FilterDataStatus;
using Http::FilterHeadersStatus;
using Http::FilterTrailersStatus;
using Http::RequestHeaderMap;
using Http::RequestTrailerMap;
using Http::ResponseHeaderMap;
using Http::ResponseTrailerMap;

static const std::string ErrorPrefix = "ext_proc_error";
static const int DefaultImmediateStatus = 200;
static const std::string FilterName = "envoy.filters.http.ext_proc";

// Changes to headers are normally tested against the MutationRules supplied
// with configuration. When writing an immediate response message, however,
// we want to support a more liberal set of rules so that filters can create
// custom error messages, and we want to prevent the MutationRules in the
// configuration from making that impossible. This is a fixed, permissive
// set of rules for that purpose.
class ImmediateMutationChecker {
public:
  ImmediateMutationChecker() {
    HeaderMutationRules rules;
    rules.mutable_allow_all_routing()->set_value(true);
    rules.mutable_allow_envoy()->set_value(true);
    rule_checker_ = std::make_unique<Checker>(rules);
  }

  const Checker& checker() const { return *rule_checker_; }

private:
  std::unique_ptr<Checker> rule_checker_;
};

void ExtProcLoggingInfo::recordGrpcCall(
    std::chrono::microseconds latency, Grpc::Status::GrpcStatus call_status,
    ProcessorState::CallbackState callback_state,
    envoy::config::core::v3::TrafficDirection traffic_direction) {
  ASSERT(callback_state != ProcessorState::CallbackState::Idle);

  // Record the gRPC call stats for the header.
  if (callback_state == ProcessorState::CallbackState::HeadersCallback) {
    if (grpcCalls(traffic_direction).header_stats_ == nullptr) {
      grpcCalls(traffic_direction).header_stats_ = std::make_unique<GrpcCall>(latency, call_status);
    }
    return;
  }

  // Record the gRPC call stats for the trailer.
  if (callback_state == ProcessorState::CallbackState::TrailersCallback) {
    if (grpcCalls(traffic_direction).trailer_stats_ == nullptr) {
      grpcCalls(traffic_direction).trailer_stats_ =
          std::make_unique<GrpcCall>(latency, call_status);
    }
    return;
  }

  // Record the gRPC call stats for the bodies.
  if (grpcCalls(traffic_direction).body_stats_ == nullptr) {
    grpcCalls(traffic_direction).body_stats_ =
        std::make_unique<GrpcCallBody>(1, call_status, latency, latency, latency);
  } else {
    auto& body_stats = grpcCalls(traffic_direction).body_stats_;
    body_stats->call_count_++;
    body_stats->total_latency_ += latency;
    body_stats->last_call_status_ = call_status;
    if (latency > body_stats->max_latency_) {
      body_stats->max_latency_ = latency;
    }
    if (latency < body_stats->min_latency_) {
      body_stats->min_latency_ = latency;
    }
  }
}

ExtProcLoggingInfo::GrpcCalls&
ExtProcLoggingInfo::grpcCalls(envoy::config::core::v3::TrafficDirection traffic_direction) {
  ASSERT(traffic_direction != envoy::config::core::v3::TrafficDirection::UNSPECIFIED);
  return traffic_direction == envoy::config::core::v3::TrafficDirection::INBOUND
             ? decoding_processor_grpc_calls_
             : encoding_processor_grpc_calls_;
}

const ExtProcLoggingInfo::GrpcCalls&
ExtProcLoggingInfo::grpcCalls(envoy::config::core::v3::TrafficDirection traffic_direction) const {
  ASSERT(traffic_direction != envoy::config::core::v3::TrafficDirection::UNSPECIFIED);
  return traffic_direction == envoy::config::core::v3::TrafficDirection::INBOUND
             ? decoding_processor_grpc_calls_
             : encoding_processor_grpc_calls_;
}

FilterConfigPerRoute::FilterConfigPerRoute(const ExtProcPerRoute& config)
    : disabled_(config.disabled()) {
  if (config.has_overrides()) {
    processing_mode_ = config.overrides().processing_mode();
  }
  if (config.overrides().has_grpc_service()) {
    grpc_service_ = config.overrides().grpc_service();
  }
}

void FilterConfigPerRoute::merge(const FilterConfigPerRoute& src) {
  disabled_ = src.disabled_;
  processing_mode_ = src.processing_mode_;
  if (src.grpcService().has_value()) {
    grpc_service_ = src.grpcService();
  }
}

void Filter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  Http::PassThroughFilter::setDecoderFilterCallbacks(callbacks);
  decoding_state_.setDecoderFilterCallbacks(callbacks);
  const Envoy::StreamInfo::FilterStateSharedPtr& filter_state =
      callbacks.streamInfo().filterState();
  if (!filter_state->hasData<ExtProcLoggingInfo>(callbacks.filterConfigName())) {
    filter_state->setData(callbacks.filterConfigName(),
                          std::make_shared<ExtProcLoggingInfo>(config_->filterMetadata()),
                          Envoy::StreamInfo::FilterState::StateType::Mutable,
                          Envoy::StreamInfo::FilterState::LifeSpan::Request);
  }
  logging_info_ = filter_state->getDataMutable<ExtProcLoggingInfo>(callbacks.filterConfigName());
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  Http::PassThroughFilter::setEncoderFilterCallbacks(callbacks);
  encoding_state_.setEncoderFilterCallbacks(callbacks);
}

Filter::StreamOpenState Filter::openStream() {
  // External processing is completed. This means there is no need to send any further
  // message to the server for processing. Just return IgnoreError so the filter
  // will return FilterHeadersStatus::Continue.
  if (processing_complete_) {
    ENVOY_LOG(debug, "External processing is completed when trying to open the gRPC stream");
    return StreamOpenState::IgnoreError;
  }
  if (!stream_) {
    ENVOY_LOG(debug, "Opening gRPC stream to external processor");
    stream_ = client_->start(*this, config_with_hash_key_, decoder_callbacks_->streamInfo());
    if (processing_complete_) {
      // Stream failed while starting and either onGrpcError or onGrpcClose was already called
      // Asserts that `stream_` is nullptr since it is not valid to be used any further
      // beyond this point.
      ASSERT(stream_ == nullptr);
      return sent_immediate_response_ ? StreamOpenState::Error : StreamOpenState::IgnoreError;
    }
    stats_.streams_started_.inc();
    // For custom access logging purposes. Applicable only for Envoy gRPC as Google gRPC does not
    // have a proper implementation of streamInfo.
    if (grpc_service_.has_envoy_grpc() && logging_info_ != nullptr) {
      logging_info_->setClusterInfo(stream_->streamInfo().upstreamClusterInfo());
    }
  }
  return StreamOpenState::Ok;
}

void Filter::closeStream() {
  if (stream_) {
    ENVOY_LOG(debug, "Calling close on stream");
    if (stream_->close()) {
      stats_.streams_closed_.inc();
    }
    stream_.reset();
  } else {
    ENVOY_LOG(debug, "Stream already closed");
  }
}

void Filter::onDestroy() {
  ENVOY_LOG(debug, "onDestroy");
  // Make doubly-sure we no longer use the stream, as
  // per the filter contract.
  processing_complete_ = true;
  closeStream();
  decoding_state_.stopMessageTimer();
  encoding_state_.stopMessageTimer();
}

FilterHeadersStatus Filter::onHeaders(ProcessorState& state,
                                      Http::RequestOrResponseHeaderMap& headers, bool end_stream) {
  switch (openStream()) {
  case StreamOpenState::Error:
    return FilterHeadersStatus::StopIteration;
  case StreamOpenState::IgnoreError:
    return FilterHeadersStatus::Continue;
  case StreamOpenState::Ok:
    // Fall through
    break;
  }

  state.setHeaders(&headers);
  state.setHasNoBody(end_stream);
  ProcessingRequest req;
  auto* headers_req = state.mutableHeaders(req);
  MutationUtils::headersToProto(headers, config_->allowedHeaders(), config_->disallowedHeaders(),
                                *headers_req->mutable_headers());
  headers_req->set_end_of_stream(end_stream);
  state.onStartProcessorCall(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout(),
                             ProcessorState::CallbackState::HeadersCallback);
  ENVOY_LOG(debug, "Sending headers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
  state.setPaused(true);
  return FilterHeadersStatus::StopIteration;
}

FilterHeadersStatus Filter::decodeHeaders(RequestHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "decodeHeaders: end_stream = {}", end_stream);
  mergePerRouteConfig();
  if (end_stream) {
    decoding_state_.setCompleteBodyAvailable(true);
  }

  FilterHeadersStatus status = FilterHeadersStatus::Continue;
  if (decoding_state_.sendHeaders()) {
    status = onHeaders(decoding_state_, headers, end_stream);
    ENVOY_LOG(trace, "onHeaders returning {}", static_cast<int>(status));
  } else {
    ENVOY_LOG(trace, "decodeHeaders: Skipped header processing");
  }

  if (!processing_complete_ && decoding_state_.shouldRemoveContentLength()) {
    headers.removeContentLength();
  }
  return status;
}

FilterDataStatus Filter::onData(ProcessorState& state, Buffer::Instance& data, bool end_stream) {
  if (end_stream) {
    state.setCompleteBodyAvailable(true);
  }
  if (state.bodyReplaced()) {
    ENVOY_LOG(trace, "Clearing body chunk because CONTINUE_AND_REPLACE was returned");
    data.drain(data.length());
    return FilterDataStatus::Continue;
  }
  if (processing_complete_) {
    ENVOY_LOG(trace, "Continuing (processing complete)");
    return FilterDataStatus::Continue;
  }

  if (state.callbackState() == ProcessorState::CallbackState::HeadersCallback) {
    ENVOY_LOG(trace, "Header processing still in progress -- holding body data");
    // We don't know what to do with the body until the response comes back.
    // We must buffer it in case we need it when that happens.
    // Raise a watermark to prevent a buffer overflow until the response comes back.
    // When end_stream is true, we need to StopIterationAndWatermark as well to stop the
    // ActiveStream from returning error when the last chunk added to stream buffer exceeds the
    // buffer limit.
    state.setPaused(true);
    state.requestWatermark();
    return FilterDataStatus::StopIterationAndWatermark;
  }

  FilterDataStatus result;
  switch (state.bodyMode()) {
  case ProcessingMode::BUFFERED:
    if (end_stream) {
      switch (openStream()) {
      case StreamOpenState::Error:
        return FilterDataStatus::StopIterationNoBuffer;
      case StreamOpenState::IgnoreError:
        return FilterDataStatus::Continue;
      case StreamOpenState::Ok:
        // Fall through
        break;
      }

      // The body has been buffered and we need to send the buffer
      ENVOY_LOG(debug, "Sending request body message");
      state.addBufferedData(data);
      auto req = setupBodyChunk(state, *state.bufferedData(), end_stream);
      sendBodyChunk(state, ProcessorState::CallbackState::BufferedBodyCallback, req);
      // Since we just just moved the data into the buffer, return NoBuffer
      // so that we do not buffer this chunk twice.
      state.setPaused(true);
      result = FilterDataStatus::StopIterationNoBuffer;
      break;
    }
    ENVOY_LOG(trace, "onData: Buffering");
    state.setPaused(true);
    result = FilterDataStatus::StopIterationAndBuffer;
    break;
  case ProcessingMode::STREAMED: {
    // STREAMED body mode works as follows:
    //
    // 1) As data callbacks come in to the filter, it "moves" the data into a new buffer, which it
    // dispatches via gRPC message to the external processor, and then keeps in a queue. It
    // may request a watermark if the queue is higher than the buffer limit to prevent running
    // out of memory.
    // 2) As a result, filters farther down the chain see empty buffers in some data callbacks.
    // 3) When a response comes back from the external processor, it injects the processor's result
    // into the filter chain using "inject**codedData". (The processor may respond indicating that
    // there is no change, which means that the original buffer stored in the queue is what gets
    // injected.)
    //
    // This way, we pipeline data from the proxy to the external processor, and give the processor
    // the ability to modify each chunk, in order. Doing this any other way would have required
    // substantial changes to the filter manager. See
    // https://github.com/envoyproxy/envoy/issues/16760 for a discussion.
    switch (openStream()) {
    case StreamOpenState::Error:
      return FilterDataStatus::StopIterationNoBuffer;
    case StreamOpenState::IgnoreError:
      return FilterDataStatus::Continue;
    case StreamOpenState::Ok:
      // Fall through
      break;
    }

    // Need to first enqueue the data into the chunk queue before sending.
    auto req = setupBodyChunk(state, data, end_stream);
    state.enqueueStreamingChunk(data, end_stream);
    sendBodyChunk(state, ProcessorState::CallbackState::StreamedBodyCallback, req);

    // At this point we will continue, but with no data, because that will come later
    if (end_stream) {
      // But we need to stop iteration for the last chunk because it's our last chance to do stuff
      state.setPaused(true);
      result = FilterDataStatus::StopIterationNoBuffer;
    } else {
      result = FilterDataStatus::Continue;
    }
    break;
  }
  case ProcessingMode::BUFFERED_PARTIAL:
    // BUFFERED_PARTIAL mode works as follows:
    //
    // 1) As data chunks arrive, we move the data into a new buffer, which we store
    // in the buffer queue, and continue the filter stream with an empty buffer. This
    // is the same thing that we do in STREAMING mode.
    // 2) If end of stream is reached before the queue reaches the buffer limit, we
    // send the buffered data to the server and essentially behave as if we are in
    // buffered mode.
    // 3) If instead the buffer limit is reached before end of stream, then we also
    // send the buffered data to the server, and raise the watermark to prevent Envoy
    // from running out of memory while we wait.
    // 4) It is possible that Envoy will keep sending us data even in that case, so
    // we must continue to queue data and prepare to re-inject it later.
    if (state.partialBodyProcessed()) {
      // We already sent and received the buffer, so everything else just falls through.
      ENVOY_LOG(trace, "Partial buffer limit reached");
      // Make sure that we do not accidentally try to modify the headers before
      // we continue, which will result in them possibly being sent.
      state.setHeaders(nullptr);
      result = FilterDataStatus::Continue;
    } else if (state.callbackState() ==
               ProcessorState::CallbackState::BufferedPartialBodyCallback) {
      // More data came in while we were waiting for a callback result. We need
      // to queue it and deliver it later in case the callback changes the data.
      state.enqueueStreamingChunk(data, end_stream);
      ENVOY_LOG(trace, "Call in progress for partial mode");
      state.setPaused(true);
      result = FilterDataStatus::StopIterationNoBuffer;
    } else {
      state.enqueueStreamingChunk(data, end_stream);
      if (end_stream || state.queueOverHighLimit()) {
        // At either end of stream or when the buffer is full, it's time to send what we have
        // to the processor.
        bool terminate;
        FilterDataStatus chunk_result;
        std::tie(terminate, chunk_result) = sendStreamChunk(state);
        if (terminate) {
          return chunk_result;
        }
      }
      result = FilterDataStatus::StopIterationNoBuffer;
    }
    break;
  case ProcessingMode::NONE:
  default:
    result = FilterDataStatus::Continue;
    break;
  }

  return result;
}

std::pair<bool, Http::FilterDataStatus> Filter::sendStreamChunk(ProcessorState& state) {
  switch (openStream()) {
  case StreamOpenState::Error:
    return {true, FilterDataStatus::StopIterationNoBuffer};
  case StreamOpenState::IgnoreError:
    return {true, FilterDataStatus::Continue};
  case StreamOpenState::Ok:
    // Fall through
    break;
  }

  const auto& all_data = state.consolidateStreamedChunks();
  ENVOY_LOG(debug, "Sending {} bytes of data in buffered partial mode. end_stream = {}",
            state.chunkQueue().receivedData().length(), all_data.end_stream);
  auto req = setupBodyChunk(state, state.chunkQueue().receivedData(), all_data.end_stream);
  sendBodyChunk(state, ProcessorState::CallbackState::BufferedPartialBodyCallback, req);
  state.setPaused(true);
  return {false, FilterDataStatus::StopIterationNoBuffer};
}

FilterDataStatus Filter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "decodeData({}): end_stream = {}", data.length(), end_stream);
  const auto status = onData(decoding_state_, data, end_stream);
  ENVOY_LOG(trace, "decodeData returning {}", static_cast<int>(status));
  return status;
}

FilterTrailersStatus Filter::onTrailers(ProcessorState& state, Http::HeaderMap& trailers) {
  if (processing_complete_) {
    ENVOY_LOG(trace, "trailers: Continue");
    return FilterTrailersStatus::Continue;
  }

  bool body_delivered = state.completeBodyAvailable();
  state.setCompleteBodyAvailable(true);
  state.setTrailersAvailable(true);
  state.setTrailers(&trailers);

  if (state.callbackState() != ProcessorState::CallbackState::Idle) {
    ENVOY_LOG(trace, "Previous callback still executing -- holding header iteration");
    state.setPaused(true);
    return FilterTrailersStatus::StopIteration;
  }

  if (!body_delivered &&
      ((state.bufferedData() && state.bodyMode() == ProcessingMode::BUFFERED) ||
       (!state.chunkQueue().empty() && state.bodyMode() == ProcessingMode::BUFFERED_PARTIAL))) {
    // If no gRPC stream yet, opens it before sending data.
    switch (openStream()) {
    case StreamOpenState::Error:
      return FilterTrailersStatus::StopIteration;
    case StreamOpenState::IgnoreError:
      return FilterTrailersStatus::Continue;
    case StreamOpenState::Ok:
      // Fall through
      break;
    }

    // We would like to process the body in a buffered way, but until now the complete
    // body has not arrived. With the arrival of trailers, we now know that the body
    // has arrived.
    if (state.bodyMode() == ProcessingMode::BUFFERED) {
      // Sending data left over in the buffer.
      auto req = setupBodyChunk(state, *state.bufferedData(), false);
      sendBodyChunk(state, ProcessorState::CallbackState::BufferedBodyCallback, req);
    } else {
      // Sending data left over in the queue.
      const auto& all_data = state.consolidateStreamedChunks();
      auto req = setupBodyChunk(state, state.chunkQueue().receivedData(), false);
      ENVOY_LOG(debug, "Sending {} bytes of data in buffered partial mode. end_stream = {}",
                state.chunkQueue().receivedData().length(), all_data.end_stream);
      sendBodyChunk(state, ProcessorState::CallbackState::BufferedPartialBodyCallback, req);
    }
    state.setPaused(true);
    return FilterTrailersStatus::StopIteration;
  }

  if (!state.sendTrailers()) {
    ENVOY_LOG(trace, "Skipped trailer processing");
    return FilterTrailersStatus::Continue;
  }

  switch (openStream()) {
  case StreamOpenState::Error:
    return FilterTrailersStatus::StopIteration;
  case StreamOpenState::IgnoreError:
    return FilterTrailersStatus::Continue;
  case StreamOpenState::Ok:
    // Fall through
    break;
  }

  sendTrailers(state, trailers);
  state.setPaused(true);
  return FilterTrailersStatus::StopIteration;
}

FilterTrailersStatus Filter::decodeTrailers(RequestTrailerMap& trailers) {
  ENVOY_LOG(trace, "decodeTrailers");
  const auto status = onTrailers(decoding_state_, trailers);
  ENVOY_LOG(trace, "encodeTrailers returning {}", static_cast<int>(status));
  return status;
}

FilterHeadersStatus Filter::encodeHeaders(ResponseHeaderMap& headers, bool end_stream) {
  ENVOY_LOG(trace, "encodeHeaders end_stream = {}", end_stream);
  // Try to merge the route config again in case the decodeHeaders() is not called when processing
  // local reply.
  mergePerRouteConfig();
  if (end_stream) {
    encoding_state_.setCompleteBodyAvailable(true);
  }

  FilterHeadersStatus status = FilterHeadersStatus::Continue;
  if (!processing_complete_ && encoding_state_.sendHeaders()) {
    status = onHeaders(encoding_state_, headers, end_stream);
    ENVOY_LOG(trace, "onHeaders returns {}", static_cast<int>(status));
  } else {
    ENVOY_LOG(trace, "encodeHeaders: Skipped header processing");
  }

  // The content-length header will be kept when either one of the following conditions is met:
  // (1) `shouldRemoveContentLength` returns false.
  // (2) side stream processing has been completed. For example, it could be caused by stream error
  // that triggers the local reply or due to spurious message that skips the side stream
  // mutation.
  if (!processing_complete_ && encoding_state_.shouldRemoveContentLength()) {
    headers.removeContentLength();
  }
  return status;
}

FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "encodeData({}): end_stream = {}", data.length(), end_stream);
  const auto status = onData(encoding_state_, data, end_stream);
  ENVOY_LOG(trace, "encodeData returning {}", static_cast<int>(status));
  return status;
}

FilterTrailersStatus Filter::encodeTrailers(ResponseTrailerMap& trailers) {
  ENVOY_LOG(trace, "encodeTrailers");
  const auto status = onTrailers(encoding_state_, trailers);
  ENVOY_LOG(trace, "encodeTrailers returning {}", static_cast<int>(status));
  return status;
}

ProcessingRequest Filter::setupBodyChunk(ProcessorState& state, const Buffer::Instance& data,
                                         bool end_stream) {
  ENVOY_LOG(debug, "Sending a body chunk of {} bytes, end_stram {}", data.length(), end_stream);
  ProcessingRequest req;
  auto* body_req = state.mutableBody(req);
  body_req->set_end_of_stream(end_stream);
  body_req->set_body(data.toString());
  return req;
}

void Filter::sendBodyChunk(ProcessorState& state, ProcessorState::CallbackState new_state,
                           ProcessingRequest& req) {
  state.onStartProcessorCall(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout(),
                             new_state);
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
}

void Filter::sendTrailers(ProcessorState& state, const Http::HeaderMap& trailers) {
  ProcessingRequest req;
  auto* trailers_req = state.mutableTrailers(req);
  MutationUtils::headersToProto(trailers, config_->allowedHeaders(), config_->disallowedHeaders(),
                                *trailers_req->mutable_trailers());
  state.onStartProcessorCall(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout(),
                             ProcessorState::CallbackState::TrailersCallback);
  ENVOY_LOG(debug, "Sending trailers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
}

void Filter::logGrpcStreamInfo() {
  if (stream_ != nullptr && logging_info_ != nullptr && grpc_service_.has_envoy_grpc()) {
    const auto& upstream_meter = stream_->streamInfo().getUpstreamBytesMeter();
    if (upstream_meter != nullptr) {
      logging_info_->setBytesSent(upstream_meter->wireBytesSent());
      logging_info_->setBytesReceived(upstream_meter->wireBytesReceived());
    }
    // Only set upstream host in logging info once.
    if (logging_info_->upstreamHost() == nullptr) {
      logging_info_->setUpstreamHost(stream_->streamInfo().upstreamInfo()->upstreamHost());
    }
  }
}

void Filter::onNewTimeout(const ProtobufWkt::Duration& override_message_timeout) {
  const auto result = DurationUtil::durationToMillisecondsNoThrow(override_message_timeout);
  if (!result.ok()) {
    ENVOY_LOG(warn, "Ext_proc server new timeout setting is out of duration range. "
                    "Ignoring the message.");
    stats_.override_message_timeout_ignored_.inc();
    return;
  }
  const auto message_timeout_ms = result.value();
  // The new timeout has to be >=1ms and <= max_message_timeout configured in filter.
  const uint64_t min_timeout_ms = 1;
  const uint64_t max_timeout_ms = config_->maxMessageTimeout();
  if (message_timeout_ms < min_timeout_ms || message_timeout_ms > max_timeout_ms) {
    ENVOY_LOG(warn, "Ext_proc server new timeout setting is out of config range. "
                    "Ignoring the message.");
    stats_.override_message_timeout_ignored_.inc();
    return;
  }
  // One of the below function call is non-op since the ext_proc filter can
  // only be in one of the below state, and just one timer is enabled.
  auto decoder_timer_restarted = decoding_state_.restartMessageTimer(message_timeout_ms);
  auto encoder_timer_restarted = encoding_state_.restartMessageTimer(message_timeout_ms);
  if (!decoder_timer_restarted && !encoder_timer_restarted) {
    stats_.override_message_timeout_ignored_.inc();
    return;
  }
  stats_.override_message_timeout_received_.inc();
}

void Filter::onReceiveMessage(std::unique_ptr<ProcessingResponse>&& r) {
  if (processing_complete_) {
    ENVOY_LOG(debug, "Ignoring stream message received after processing complete");
    // Ignore additional messages after we decided we were done with the stream
    return;
  }

  auto response = std::move(r);

  // Check whether the server is asking to extend the timer.
  if (response->has_override_message_timeout()) {
    onNewTimeout(response->override_message_timeout());
    return;
  }

  // Update processing mode now because filter callbacks check it
  // and the various "handle" methods below may result in callbacks
  // being invoked in line. This only happens when filter has allow_mode_override
  // set to true and filter is waiting for header processing response.
  // Otherwise, the response mode_override proto field is ignored.
  if (config_->allowModeOverride() && inHeaderProcessState() && response->has_mode_override()) {
    ENVOY_LOG(debug, "Processing mode overridden by server for this request");
    decoding_state_.setProcessingMode(response->mode_override());
    encoding_state_.setProcessingMode(response->mode_override());
  }

  ENVOY_LOG(debug, "Received {} response", responseCaseToString(response->response_case()));
  absl::Status processing_status;
  switch (response->response_case()) {
  case ProcessingResponse::ResponseCase::kRequestHeaders:
    processing_status = decoding_state_.handleHeadersResponse(response->request_headers());
    break;
  case ProcessingResponse::ResponseCase::kResponseHeaders:
    processing_status = encoding_state_.handleHeadersResponse(response->response_headers());
    break;
  case ProcessingResponse::ResponseCase::kRequestBody:
    processing_status = decoding_state_.handleBodyResponse(response->request_body());
    break;
  case ProcessingResponse::ResponseCase::kResponseBody:
    processing_status = encoding_state_.handleBodyResponse(response->response_body());
    break;
  case ProcessingResponse::ResponseCase::kRequestTrailers:
    processing_status = decoding_state_.handleTrailersResponse(response->request_trailers());
    break;
  case ProcessingResponse::ResponseCase::kResponseTrailers:
    processing_status = encoding_state_.handleTrailersResponse(response->response_trailers());
    break;
  case ProcessingResponse::ResponseCase::kImmediateResponse:
    if (config_->disableImmediateResponse()) {
      ENVOY_LOG(debug, "Filter has disable_immediate_response configured. "
                       "Treat the immediate response message as spurious response.");
      processing_status =
          absl::FailedPreconditionError("unhandled immediate response due to config disabled it");
    } else {
      // We won't be sending anything more to the stream after we
      // receive this message.
      ENVOY_LOG(debug, "Sending immediate response");
      processing_complete_ = true;
      onFinishProcessorCalls(Grpc::Status::Ok);
      closeStream();
      sendImmediateResponse(response->immediate_response());
      processing_status = absl::OkStatus();
    }
    break;
  default:
    // Any other message is considered spurious
    ENVOY_LOG(debug, "Received unknown stream message {} -- ignoring and marking spurious",
              response->response_case());
    processing_status = absl::FailedPreconditionError("unhandled message");
    break;
  }

  if (processing_status.ok()) {
    stats_.stream_msgs_received_.inc();
  } else if (absl::IsFailedPrecondition(processing_status)) {
    // Processing code uses this specific error code in the case that a
    // message was received out of order.
    stats_.spurious_msgs_received_.inc();
    // When a message is received out of order, ignore it and also
    // ignore the stream for the rest of this filter instance's lifetime
    // to protect us from a malformed server.
    ENVOY_LOG(warn, "Spurious response message {} received on gRPC stream",
              response->response_case());
    closeStream();
    clearAsyncState();
    processing_complete_ = true;
  } else {
    // Any other error results in an immediate response with an error message.
    // This could happen, for example, after a header mutation is rejected.
    ENVOY_LOG(debug, "Sending immediate response: {}", processing_status.message());
    stats_.stream_msgs_received_.inc();
    processing_complete_ = true;
    onFinishProcessorCalls(processing_status.raw_code());
    closeStream();
    ImmediateResponse invalid_mutation_response;
    invalid_mutation_response.mutable_status()->set_code(StatusCode::InternalServerError);
    invalid_mutation_response.set_details(std::string(processing_status.message()));
    sendImmediateResponse(invalid_mutation_response);
  }
}

void Filter::onGrpcError(Grpc::Status::GrpcStatus status) {
  ENVOY_LOG(debug, "Received gRPC error on stream: {}", status);
  stats_.streams_failed_.inc();

  if (processing_complete_) {
    return;
  }

  if (config_->failureModeAllow()) {
    // Ignore this and treat as a successful close
    onGrpcClose();
    stats_.failure_mode_allowed_.inc();

  } else {
    processing_complete_ = true;
    // Since the stream failed, there is no need to handle timeouts, so
    // make sure that they do not fire now.
    onFinishProcessorCalls(status);
    closeStream();
    ImmediateResponse errorResponse;
    errorResponse.mutable_status()->set_code(StatusCode::InternalServerError);
    errorResponse.set_details(absl::StrFormat("%s_gRPC_error_%i", ErrorPrefix, status));
    sendImmediateResponse(errorResponse);
  }
}

void Filter::onGrpcClose() {
  ENVOY_LOG(debug, "Received gRPC stream close");

  processing_complete_ = true;
  stats_.streams_closed_.inc();
  // Successful close. We can ignore the stream for the rest of our request
  // and response processing.
  closeStream();
  clearAsyncState();
}

void Filter::onMessageTimeout() {
  ENVOY_LOG(debug, "message timeout reached");
  logGrpcStreamInfo();
  stats_.message_timeouts_.inc();
  if (config_->failureModeAllow()) {
    // The user would like a timeout to not cause message processing to fail.
    // However, we don't know if the external processor will send a response later,
    // and we can't wait any more. So, as we do for a spurious message, ignore
    // the external processor for the rest of the request.
    processing_complete_ = true;
    closeStream();
    stats_.failure_mode_allowed_.inc();
    clearAsyncState();

  } else {
    // Return an error and stop processing the current stream.
    processing_complete_ = true;
    closeStream();
    decoding_state_.onFinishProcessorCall(Grpc::Status::DeadlineExceeded);
    encoding_state_.onFinishProcessorCall(Grpc::Status::DeadlineExceeded);
    ImmediateResponse errorResponse;
    errorResponse.mutable_status()->set_code(StatusCode::InternalServerError);
    errorResponse.set_details(absl::StrFormat("%s_per-message_timeout_exceeded", ErrorPrefix));
    sendImmediateResponse(errorResponse);
  }
}

// Regardless of the current filter state, reset it to "IDLE", continue
// the current callback, and reset timers. This is used in a few error-handling situations.
void Filter::clearAsyncState() {
  decoding_state_.clearAsyncState();
  encoding_state_.clearAsyncState();
}

// Regardless of the current state, ensure that the timers won't fire
// again.
void Filter::onFinishProcessorCalls(Grpc::Status::GrpcStatus call_status) {
  decoding_state_.onFinishProcessorCall(call_status);
  encoding_state_.onFinishProcessorCall(call_status);
}

static const ImmediateMutationChecker& immediateResponseChecker() {
  CONSTRUCT_ON_FIRST_USE(ImmediateMutationChecker);
}

void Filter::sendImmediateResponse(const ImmediateResponse& response) {
  auto status_code = response.has_status() ? response.status().code() : DefaultImmediateStatus;
  if (!MutationUtils::isValidHttpStatus(status_code)) {
    ENVOY_LOG(debug, "Ignoring attempt to set invalid HTTP status {}", status_code);
    status_code = DefaultImmediateStatus;
  }
  const auto grpc_status =
      response.has_grpc_status()
          ? absl::optional<Grpc::Status::GrpcStatus>(response.grpc_status().status())
          : absl::nullopt;
  const auto mutate_headers = [this, &response](Http::ResponseHeaderMap& headers) {
    if (response.has_headers()) {
      absl::Status mut_status;
      if (Runtime::runtimeFeatureEnabled(
              "envoy.reloadable_features.immediate_response_use_filter_mutation_rule")) {
        mut_status = MutationUtils::applyHeaderMutations(response.headers(), headers, false,
                                                         config().mutationChecker(),
                                                         stats_.rejected_header_mutations_);
      } else {
        mut_status = MutationUtils::applyHeaderMutations(response.headers(), headers, false,
                                                         immediateResponseChecker().checker(),
                                                         stats_.rejected_header_mutations_);
      }
      if (!mut_status.ok()) {
        ENVOY_LOG_EVERY_POW_2(error, "Immediate response mutations failed with {}",
                              mut_status.message());
      }
    }
  };

  sent_immediate_response_ = true;
  ENVOY_LOG(debug, "Sending local reply with status code {}", status_code);
  const auto details = StringUtil::replaceAllEmptySpace(response.details());
  encoder_callbacks_->sendLocalReply(static_cast<Http::Code>(status_code), response.body(),
                                     mutate_headers, grpc_status, details);
}

static ProcessingMode allDisabledMode() {
  ProcessingMode pm;
  pm.set_request_header_mode(ProcessingMode::SKIP);
  pm.set_response_header_mode(ProcessingMode::SKIP);
  return pm;
}

void Filter::mergePerRouteConfig() {
  if (route_config_merged_) {
    return;
  }
  route_config_merged_ = true;

  auto merged_config = Http::Utility::getMergedPerFilterConfig<FilterConfigPerRoute>(
      decoder_callbacks_,
      [](FilterConfigPerRoute& dst, const FilterConfigPerRoute& src) { dst.merge(src); });
  if (!merged_config.has_value()) {
    return;
  }
  if (merged_config->disabled()) {
    // Rather than introduce yet another flag, use the processing mode
    // structure to disable all the callbacks.
    ENVOY_LOG(trace, "Disabling filter due to per-route configuration");
    const auto all_disabled = allDisabledMode();
    decoding_state_.setProcessingMode(all_disabled);
    encoding_state_.setProcessingMode(all_disabled);
    return;
  }
  if (merged_config->processingMode()) {
    ENVOY_LOG(trace, "Setting new processing mode from per-route configuration");
    decoding_state_.setProcessingMode(*(merged_config->processingMode()));
    encoding_state_.setProcessingMode(*(merged_config->processingMode()));
  }
  if (merged_config->grpcService()) {
    ENVOY_LOG(trace, "Setting new GrpcService from per-route configuration");
    grpc_service_ = *merged_config->grpcService();
    config_with_hash_key_.setConfig(*merged_config->grpcService());
  }
}

std::string responseCaseToString(const ProcessingResponse::ResponseCase response_case) {
  switch (response_case) {
  case ProcessingResponse::ResponseCase::kRequestHeaders:
    return "request headers";
  case ProcessingResponse::ResponseCase::kResponseHeaders:
    return "response headers";
  case ProcessingResponse::ResponseCase::kRequestBody:
    return "request body";
  case ProcessingResponse::ResponseCase::kResponseBody:
    return "response body";
  case ProcessingResponse::ResponseCase::kRequestTrailers:
    return "request trailers";
  case ProcessingResponse::ResponseCase::kResponseTrailers:
    return "response trailers";
  case ProcessingResponse::ResponseCase::kImmediateResponse:
    return "immediate response";
  default:
    return "unknown";
  }
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
