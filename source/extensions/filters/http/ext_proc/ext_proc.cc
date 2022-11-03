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
  grpcCalls(traffic_direction).emplace_back(latency, call_status, callback_state);
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
  if (!filter_state->hasData<ExtProcLoggingInfo>(ExtProcLoggingInfoName)) {
    filter_state->setData(ExtProcLoggingInfoName, std::make_shared<ExtProcLoggingInfo>(),
                          Envoy::StreamInfo::FilterState::StateType::Mutable,
                          Envoy::StreamInfo::FilterState::LifeSpan::Request);
  }
  logging_info_ = filter_state->getDataMutable<ExtProcLoggingInfo>(ExtProcLoggingInfoName);
}

void Filter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  Http::PassThroughFilter::setEncoderFilterCallbacks(callbacks);
  encoding_state_.setEncoderFilterCallbacks(callbacks);
}

Filter::StreamOpenState Filter::openStream() {
  ENVOY_BUG(!processing_complete_, "openStream should not have been called");
  if (!stream_) {
    ENVOY_LOG(debug, "Opening gRPC stream to external processor");
    stream_ = client_->start(*this, grpc_service_, decoder_callbacks_->streamInfo());
    stats_.streams_started_.inc();
    if (processing_complete_) {
      // Stream failed while starting and either onGrpcError or onGrpcClose was already called
      return sent_immediate_response_ ? StreamOpenState::Error : StreamOpenState::IgnoreError;
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
  MutationUtils::headersToProto(headers, *headers_req->mutable_headers());
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

  if (!decoding_state_.sendHeaders()) {
    ENVOY_LOG(trace, "decodeHeaders: Skipped");
    return FilterHeadersStatus::Continue;
  }

  const auto status = onHeaders(decoding_state_, headers, end_stream);
  ENVOY_LOG(trace, "decodeHeaders returning {}", static_cast<int>(status));
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
  bool just_added_trailers = false;
  Http::HeaderMap* new_trailers = nullptr;
  if (end_stream && state.sendTrailers()) {
    // We're at the end of the stream, but the filter wants to process trailers.
    // According to the filter contract, this is the only place where we can
    // add trailers, even if we will return right after this and process them
    // later.
    ENVOY_LOG(trace, "Creating new, empty trailers");
    new_trailers = state.addTrailers();
    state.setTrailersAvailable(true);
    just_added_trailers = true;
  }
  if (state.callbackState() == ProcessorState::CallbackState::HeadersCallback) {
    ENVOY_LOG(trace, "Header processing still in progress -- holding body data");
    // We don't know what to do with the body until the response comes back.
    // We must buffer it in case we need it when that happens.
    if (end_stream) {
      state.setPaused(true);
      return FilterDataStatus::StopIterationAndBuffer;
    } else {
      // Raise a watermark to prevent a buffer overflow until the response comes back.
      state.setPaused(true);
      state.requestWatermark();
      return FilterDataStatus::StopIterationAndWatermark;
    }
  }
  if (state.callbackState() == ProcessorState::CallbackState::StreamedBodyCallbackFinishing) {
    // We were previously streaming the body, but there are more chunks waiting
    // to be processed, so we can't send the body yet.
    // Move the data for our chunk into a queue so that we can re-inject it later
    // when the processor returns. See the comments below for more details on how
    // this works in general.
    ENVOY_LOG(trace, "Enqueuing data while we wait for processing to finish");
    state.enqueueStreamingChunk(data, end_stream, false);
    if (end_stream) {
      // But we need to buffer the last chunk because it's our last chance to do stuff
      state.setPaused(true);
      return FilterDataStatus::StopIterationNoBuffer;
    } else {
      return FilterDataStatus::Continue;
    }
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
      sendBodyChunk(state, *state.bufferedData(),
                    ProcessorState::CallbackState::BufferedBodyCallback, true);
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
    // Send the chunk on the gRPC stream
    sendBodyChunk(state, data, ProcessorState::CallbackState::StreamedBodyCallback, end_stream);
    // Move the data to the queue and optionally raise the watermark.
    state.enqueueStreamingChunk(data, end_stream, true);

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
      result = FilterDataStatus::Continue;
    } else if (state.callbackState() ==
               ProcessorState::CallbackState::BufferedPartialBodyCallback) {
      // More data came in while we were waiting for a callback result. We need
      // to queue it and deliver it later in case the callback changes the data.
      state.enqueueStreamingChunk(data, false, false);
      ENVOY_LOG(trace, "Call in progress for partial mode");
      state.setPaused(true);
      result = FilterDataStatus::StopIterationNoBuffer;
    } else if (end_stream || state.queueOverHighLimit()) {
      bool terminate;
      std::tie(terminate, result) = sendStreamChunk(state, data, end_stream);

      if (terminate) {
        return result;
      }
    } else {
      // Keep on running and buffering
      state.enqueueStreamingChunk(data, false, false);

      if (Runtime::runtimeFeatureEnabled(Runtime::defer_processing_backedup_streams) &&
          state.queueOverHighLimit()) {
        // When we transition to queue over high limit, we read disable the
        // stream. With deferred processing, this means new data will buffer in
        // the receiving codec buffer (not reaching this filter) and data
        // already queued in this filter hasn't yet been sent externally.
        //
        // The filter would send the queued data if it was invoked again, or if
        // we explicitly kick it off. The former wouldn't happen with deferred
        // processing since we would be buffering in the receiving codec buffer,
        // so we opt for the latter, explicitly kicking it off.
        bool terminate;
        Buffer::OwnedImpl empty_buffer{};
        std::tie(terminate, result) = sendStreamChunk(state, empty_buffer, false);

        if (terminate) {
          return result;
        }
      } else {
        result = FilterDataStatus::Continue;
      }
    }
    break;
  case ProcessingMode::NONE:
  default:
    result = FilterDataStatus::Continue;
    break;
  }
  if (just_added_trailers) {
    // If we get here, then we need to send the trailers message now
    switch (openStream()) {
    case StreamOpenState::Error:
      return FilterDataStatus::StopIterationNoBuffer;
    case StreamOpenState::IgnoreError:
      return FilterDataStatus::Continue;
    case StreamOpenState::Ok:
      // Fall through
      break;
    }
    sendTrailers(state, *new_trailers);
    state.setPaused(true);
    return FilterDataStatus::StopIterationAndBuffer;
  }
  return result;
}

std::pair<bool, Http::FilterDataStatus>
Filter::sendStreamChunk(ProcessorState& state, Buffer::Instance& data, bool end_stream) {
  switch (openStream()) {
  case StreamOpenState::Error:
    return {true, FilterDataStatus::StopIterationNoBuffer};
  case StreamOpenState::IgnoreError:
    return {true, FilterDataStatus::Continue};
  case StreamOpenState::Ok:
    // Fall through
    break;
  }
  state.enqueueStreamingChunk(data, false, false);
  // Put all buffered data so far into one big buffer
  const auto& all_data = state.consolidateStreamedChunks(true);
  ENVOY_LOG(debug, "Sending {} bytes of data in buffered partial mode. end_stream = {}",
            all_data.data.length(), end_stream);
  sendBodyChunk(state, all_data.data, ProcessorState::CallbackState::BufferedPartialBodyCallback,
                end_stream);
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

  if (state.callbackState() == ProcessorState::CallbackState::HeadersCallback ||
      state.callbackState() == ProcessorState::CallbackState::BufferedBodyCallback) {
    ENVOY_LOG(trace, "Previous callback still executing -- holding header iteration");
    state.setPaused(true);
    return FilterTrailersStatus::StopIteration;
  }

  if (!body_delivered && state.bodyMode() == ProcessingMode::BUFFERED) {
    // We would like to process the body in a buffered way, but until now the complete
    // body has not arrived. With the arrival of trailers, we now know that the body
    // has arrived.
    sendBufferedData(state, ProcessorState::CallbackState::BufferedBodyCallback, true);
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
  if (end_stream) {
    encoding_state_.setCompleteBodyAvailable(true);
  }

  if (processing_complete_ || !encoding_state_.sendHeaders()) {
    ENVOY_LOG(trace, "encodeHeaders: Continue");
    return FilterHeadersStatus::Continue;
  }

  const auto status = onHeaders(encoding_state_, headers, end_stream);
  ENVOY_LOG(trace, "encodeHeaders returns {}", static_cast<int>(status));
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

void Filter::sendBodyChunk(ProcessorState& state, const Buffer::Instance& data,
                           ProcessorState::CallbackState new_state, bool end_stream) {
  ENVOY_LOG(debug, "Sending a body chunk of {} bytes", data.length());
  state.onStartProcessorCall(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout(),
                             new_state);
  ProcessingRequest req;
  auto* body_req = state.mutableBody(req);
  body_req->set_end_of_stream(end_stream);
  body_req->set_body(data.toString());
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
}

void Filter::sendTrailers(ProcessorState& state, const Http::HeaderMap& trailers) {
  ProcessingRequest req;
  auto* trailers_req = state.mutableTrailers(req);
  MutationUtils::headersToProto(trailers, *trailers_req->mutable_trailers());
  state.onStartProcessorCall(std::bind(&Filter::onMessageTimeout, this), config_->messageTimeout(),
                             ProcessorState::CallbackState::TrailersCallback);
  ENVOY_LOG(debug, "Sending trailers message");
  stream_->send(std::move(req), false);
  stats_.stream_msgs_sent_.inc();
}

void Filter::onReceiveMessage(std::unique_ptr<ProcessingResponse>&& r) {
  if (processing_complete_) {
    ENVOY_LOG(debug, "Ignoring stream message received after processing complete");
    // Ignore additional messages after we decided we were done with the stream
    return;
  }

  auto response = std::move(r);

  // Update processing mode now because filter callbacks check it
  // and the various "handle" methods below may result in callbacks
  // being invoked in line.
  if (response->has_mode_override()) {
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
    // We won't be sending anything more to the stream after we
    // receive this message.
    ENVOY_LOG(debug, "Sending immediate response");
    processing_complete_ = true;
    closeStream();
    onFinishProcessorCalls(Grpc::Status::Ok);
    sendImmediateResponse(response->immediate_response());
    processing_status = absl::OkStatus();
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
    closeStream();
    onFinishProcessorCalls(processing_status.raw_code());
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
    closeStream();
    // Since the stream failed, there is no need to handle timeouts, so
    // make sure that they do not fire now.
    onFinishProcessorCalls(status);
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
      const auto mut_status = MutationUtils::applyHeaderMutations(
          response.headers(), headers, false, immediateResponseChecker().checker(),
          stats_.rejected_header_mutations_);
      ENVOY_BUG(mut_status.ok(), "Immediate response mutations should not fail");
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
  auto&& merged_config = Http::Utility::getMergedPerFilterConfig<FilterConfigPerRoute>(
      decoder_callbacks_,
      [](FilterConfigPerRoute& dst, const FilterConfigPerRoute& src) { dst.merge(src); });
  if (!merged_config) {
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
