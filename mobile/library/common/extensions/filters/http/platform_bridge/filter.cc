#include "library/common/extensions/filters/http/platform_bridge/filter.h"

#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/dump_state_utils.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/common/utility.h"

#include "library/common/api/external.h"
#include "library/common/bridge/utility.h"
#include "library/common/buffer/bridge_fragment.h"
#include "library/common/data/utility.h"
#include "library/common/extensions/filters/http/platform_bridge/c_type_definitions.h"
#include "library/common/http/header_utility.h"
#include "library/common/http/headers.h"
#include "library/common/stream_info/extra_stream_info.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PlatformBridge {

namespace {
// TODO: https://github.com/envoyproxy/envoy-mobile/issues/1287
void replaceHeaders(Http::HeaderMap& headers, envoy_headers c_headers) {
  headers.clear();
  for (envoy_map_size_t i = 0; i < c_headers.length; i++) {
    headers.addCopy(Http::LowerCaseString(Data::Utility::copyToString(c_headers.entries[i].key)),
                    Data::Utility::copyToString(c_headers.entries[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(c_headers);
}

} // namespace

static void envoy_filter_release_callbacks(const void* context) {
  PlatformBridgeFilterWeakPtr* weak_filter =
      static_cast<PlatformBridgeFilterWeakPtr*>(const_cast<void*>(context));
  delete weak_filter;
}

static void envoy_filter_callback_resume_decoding(const void* context) {
  PlatformBridgeFilterWeakPtr* weak_filter =
      static_cast<PlatformBridgeFilterWeakPtr*>(const_cast<void*>(context));
  if (auto filter = weak_filter->lock()) {
    filter->resumeDecoding();
  }
}

static void envoy_filter_callback_resume_encoding(const void* context) {
  PlatformBridgeFilterWeakPtr* weak_filter =
      static_cast<PlatformBridgeFilterWeakPtr*>(const_cast<void*>(context));
  if (auto filter = weak_filter->lock()) {
    filter->resumeEncoding();
  }
}

static void envoy_filter_reset_idle(const void* context) {
  PlatformBridgeFilterWeakPtr* weak_filter =
      static_cast<PlatformBridgeFilterWeakPtr*>(const_cast<void*>(context));
  if (auto filter = weak_filter->lock()) {
    filter->resetIdleTimer();
  }
}

PlatformBridgeFilterConfig::PlatformBridgeFilterConfig(
    const envoymobile::extensions::filters::http::platform_bridge::PlatformBridge& proto_config)
    : filter_name_(proto_config.platform_filter_name()),
      platform_filter_(static_cast<envoy_http_filter*>(
          Api::External::retrieveApi(proto_config.platform_filter_name()))) {}

PlatformBridgeFilter::PlatformBridgeFilter(PlatformBridgeFilterConfigSharedPtr config,
                                           Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher), filter_name_(config->filter_name()),
      platform_filter_(*config->platform_filter()) {
  // The initialization above sets platform_filter_ to a copy of the struct stored on the config.
  // In the typical case, this will represent a filter implementation that needs to be intantiated.
  // static_context will contain the necessary platform-specific mechanism to produce a filter
  // instance. instance_context will initially be null, but after initialization, set to the
  // context needed for actual filter invocations.
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::PlatformBridgeFilter", filter_name_);

  if (platform_filter_.init_filter) {
    // Set the instance_context to the result of the initialization call. Cleanup will ultimately
    // occur within the onDestroy() invocation below.
    ENVOY_LOG(trace, "PlatformBridgeFilter({})->init_filter", filter_name_);
    platform_filter_.instance_context = platform_filter_.init_filter(&platform_filter_);
    ASSERT(platform_filter_.instance_context,
           fmt::format("PlatformBridgeFilter({}): init_filter unsuccessful", filter_name_));
  } else {
    // If init_filter is missing, zero out the rest of the struct for safety.
    ENVOY_LOG(debug, "PlatformBridgeFilter({}): missing initializer", filter_name_);
    platform_filter_ = {};
  }

  // Set directional filters now that the platform_filter_ has been updated (initialized or zero'ed
  // out).
  request_filter_base_ = std::make_unique<RequestFilterBase>(*this);
  response_filter_base_ = std::make_unique<ResponseFilterBase>(*this);
}

void PlatformBridgeFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::setDecoderCallbacks", filter_name_);
  decoder_callbacks_ = &callbacks;

  // TODO(goaway): currently both platform APIs unconditionally set this field, meaning that the
  // heap allocation below occurs when it could be avoided.
  if (platform_filter_.set_request_callbacks) {
    platform_request_callbacks_.resume_iteration = envoy_filter_callback_resume_decoding;
    platform_request_callbacks_.reset_idle = envoy_filter_reset_idle;
    platform_request_callbacks_.release_callbacks = envoy_filter_release_callbacks;
    // We use a weak_ptr wrapper for the filter to ensure presence before dispatching callbacks.
    // The weak_ptr is heap-allocated, because it must be managed (and eventually released) by
    // platform code.
    platform_request_callbacks_.callback_context =
        new PlatformBridgeFilterWeakPtr{shared_from_this()};
    ENVOY_LOG(trace, "PlatformBridgeFilter({})->set_request_callbacks", filter_name_);
    platform_filter_.set_request_callbacks(platform_request_callbacks_,
                                           platform_filter_.instance_context);
  }
}

void PlatformBridgeFilter::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::setEncoderCallbacks", filter_name_);
  encoder_callbacks_ = &callbacks;

  // TODO(goaway): currently both platform APIs unconditionally set this field, meaning that the
  // heap allocation below occurs when it could be avoided.
  if (platform_filter_.set_response_callbacks) {
    platform_response_callbacks_.resume_iteration = envoy_filter_callback_resume_encoding;
    platform_response_callbacks_.reset_idle = envoy_filter_reset_idle;
    platform_response_callbacks_.release_callbacks = envoy_filter_release_callbacks;
    // We use a weak_ptr wrapper for the filter to ensure presence before dispatching callbacks.
    // The weak_ptr is heap-allocated, because it must be managed (and eventually released) by
    // platform code.
    platform_response_callbacks_.callback_context =
        new PlatformBridgeFilterWeakPtr{shared_from_this()};
    ENVOY_LOG(trace, "PlatformBridgeFilter({})->set_response_callbacks", filter_name_);
    platform_filter_.set_response_callbacks(platform_response_callbacks_,
                                            platform_filter_.instance_context);
  }
}

void PlatformBridgeFilter::onDestroy() {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::onDestroy", filter_name_);
  alive_ = false;

  // If the filter chain is destroyed before a response is received, treat as cancellation.
  if (!response_filter_base_->state_.stream_complete_ && platform_filter_.on_cancel) {
    ENVOY_LOG(trace, "PlatformBridgeFilter({})->on_cancel", filter_name_);
    platform_filter_.on_cancel(streamIntel(), finalStreamIntel(),
                               platform_filter_.instance_context);
  }

  // Allow nullptr as no-op only if nothing was initialized.
  if (platform_filter_.release_filter == nullptr) {
    ASSERT(!platform_filter_.instance_context,
           fmt::format("PlatformBridgeFilter({}): release_filter required", filter_name_));
    return;
  }

  ENVOY_LOG(trace, "PlatformBridgeFilter({})->release_filter", filter_name_);
  platform_filter_.release_filter(platform_filter_.instance_context);
  platform_filter_.instance_context = nullptr;
}

Http::LocalErrorStatus PlatformBridgeFilter::onLocalReply(const LocalReplyData& reply) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::onLocalReply", filter_name_);
  response_filter_base_->state_.stream_complete_ = true;
  auto& info = decoder_callbacks_->streamInfo();
  // TODO(goaway): set responseCode in upstream Envoy when responseCodDetails are set.
  // ASSERT(static_cast<uint32_t>(reply.code_) == info.responseCode());
  // TODO(goaway): follow up on the underscore discrepancy between these values.
  // ASSERT(reply.details_ == info.responseCodeDetails());

  if (platform_filter_.on_error) {
    envoy_error_code_t error_code = Bridge::Utility::errorCodeFromLocalStatus(reply.code_);
    envoy_data error_message = Data::Utility::copyToBridgeData(reply.details_);
    int32_t attempts = static_cast<int32_t>(info.attemptCount().value_or(0));
    platform_filter_.on_error({error_code, error_message, attempts}, streamIntel(),
                              finalStreamIntel(), platform_filter_.instance_context);
  }

  return Http::LocalErrorStatus::ContinueAndResetStream;
}

envoy_final_stream_intel PlatformBridgeFilter::finalStreamIntel() {
  RELEASE_ASSERT(decoder_callbacks_, "StreamInfo accessed before filter callbacks are set");
  // FIXME: Stream handle cannot currently be set from the filter context.
  envoy_final_stream_intel final_stream_intel{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 0, 0};
  setFinalStreamIntel(decoder_callbacks_->streamInfo(), final_stream_intel);
  return final_stream_intel;
}

envoy_stream_intel PlatformBridgeFilter::streamIntel() {
  RELEASE_ASSERT(decoder_callbacks_, "StreamInfo accessed before filter callbacks are set");
  auto& info = decoder_callbacks_->streamInfo();
  // FIXME: Stream handle cannot currently be set from the filter context.
  envoy_stream_intel stream_intel{-1, -1, 0};
  if (info.upstreamInfo()) {
    stream_intel.connection_id = info.upstreamInfo()->upstreamConnectionId().value_or(-1);
  }
  stream_intel.attempt_count = info.attemptCount().value_or(0);
  return stream_intel;
}

void PlatformBridgeFilter::dumpState(std::ostream& os, int indent_level) const {
  std::stringstream ss;
  const char* spaces = spacesForLevel(indent_level);

  ss << spaces << "PlatformBridgeFilter" << DUMP_MEMBER(filter_name_)
     << DUMP_MEMBER(error_response_) << std::endl;

  const char* inner_spaces = spacesForLevel(indent_level + 1);
  if (request_filter_base_) {
    ss << inner_spaces << "Request Filter";
    request_filter_base_->dumpState(ss, 0);
  }
  if (response_filter_base_) {
    ss << inner_spaces << "Response Filter";
    response_filter_base_->dumpState(ss, 0);
  }

  // TODO(junr03): only output to ostream arg
  // https://github.com/envoyproxy/envoy-mobile/issues/1497.
  ENVOY_LOG(error, "\n{}", ss.str());
  os << ss.str();
}

Http::FilterHeadersStatus PlatformBridgeFilter::FilterBase::onHeaders(Http::HeaderMap& headers,
                                                                      bool end_stream) {
  ScopeTrackerScopeState scope(&parent_, parent_.scopeTracker());
  state_.stream_complete_ = end_stream;

  // Allow nullptr to act as no-op.
  if (on_headers_ == nullptr) {
    state_.headers_forwarded_ = true;
    return Http::FilterHeadersStatus::Continue;
  }

  envoy_headers in_headers = Http::Utility::toBridgeHeaders(headers);
  ENVOY_LOG(trace, "PlatformBridgeFilter({})->on_*_headers", parent_.filter_name_);
  envoy_filter_headers_status result =
      on_headers_(in_headers, end_stream, streamIntel(), parent_.platform_filter_.instance_context);
  state_.on_headers_called_ = true;

  switch (result.status) {
  case kEnvoyFilterHeadersStatusContinue:
    replaceHeaders(headers, result.headers);
    state_.headers_forwarded_ = true;
    return Http::FilterHeadersStatus::Continue;

  case kEnvoyFilterHeadersStatusStopIteration:
    pending_headers_ = &headers;
    state_.iteration_state_ = IterationState::Stopped;
    ASSERT(result.headers.length == 0 && result.headers.entries == NULL);
    return Http::FilterHeadersStatus::StopIteration;

  default:
    PANIC("invalid filter state: unsupported status for platform filters");
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

Http::FilterDataStatus PlatformBridgeFilter::FilterBase::onData(Buffer::Instance& data,
                                                                bool end_stream) {
  ScopeTrackerScopeState scope(&parent_, parent_.scopeTracker());
  state_.stream_complete_ = end_stream;

  // Allow nullptr to act as no-op.
  if (on_data_ == nullptr) {
    state_.data_forwarded_ = true;
    return Http::FilterDataStatus::Continue;
  }

  auto internal_buffer = buffer();
  envoy_data in_data;

  // Decide whether to preemptively buffer data to present aggregate to platform.
  bool prebuffer_data = state_.iteration_state_ == IterationState::Stopped && internal_buffer &&
                        &data != internal_buffer && internal_buffer->length() > 0;

  if (prebuffer_data) {
    internal_buffer->move(data);
    in_data = Data::Utility::copyToBridgeData(*internal_buffer);
  } else {
    in_data = Data::Utility::copyToBridgeData(data);
  }

  ENVOY_LOG(trace, "PlatformBridgeFilter({})->on_*_data", parent_.filter_name_);
  envoy_filter_data_status result =
      on_data_(in_data, end_stream, streamIntel(), parent_.platform_filter_.instance_context);
  state_.on_data_called_ = true;

  switch (result.status) {
  case kEnvoyFilterDataStatusContinue:
    RELEASE_ASSERT(state_.iteration_state_ != IterationState::Stopped,
                   "invalid filter state: filter iteration must be resumed with ResumeIteration");
    data.drain(data.length());
    data.addBufferFragment(*Buffer::BridgeFragment::createBridgeFragment(result.data));
    state_.data_forwarded_ = true;
    return Http::FilterDataStatus::Continue;

  case kEnvoyFilterDataStatusStopIterationAndBuffer:
    if (prebuffer_data) {
      // Data will already have been added to the internal buffer (above).
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    // Data will be buffered on return.
    state_.iteration_state_ = IterationState::Stopped;
    return Http::FilterDataStatus::StopIterationAndBuffer;

  case kEnvoyFilterDataStatusStopIterationNoBuffer:
    // In this context all previously buffered data can/should be dropped. If no data has been
    // buffered, this is a no-op. If data was previously buffered, the most likely case is
    // that a filter has decided to handle generating a response itself and no longer needs it.
    // We opt for making this assumption since it's otherwise ambiguous how we should handle
    // buffering when switching between the two stopped states, and since data can be arbitrarily
    // interleaved, it's unclear that there's any legitimate case to support any more complex
    // behavior.
    if (internal_buffer) {
      internal_buffer->drain(internal_buffer->length());
    }
    state_.iteration_state_ = IterationState::Stopped;
    return Http::FilterDataStatus::StopIterationNoBuffer;

  // Resume previously-stopped iteration, possibly forwarding headers if iteration was stopped
  // during an on*Headers invocation.
  case kEnvoyFilterDataStatusResumeIteration:
    RELEASE_ASSERT(state_.iteration_state_ == IterationState::Stopped,
                   "invalid filter state: ResumeIteration may only be used when filter iteration "
                   "is stopped");
    // Update pending henders before resuming iteration, if needed.
    if (result.pending_headers) {
      replaceHeaders(*pending_headers_, *result.pending_headers);
      pending_headers_ = nullptr;
      free(result.pending_headers);
    }
    // We've already moved data into the internal buffer and presented it to the platform. Replace
    // the internal buffer with any modifications returned by the platform filter prior to
    // resumption.
    if (internal_buffer) {
      internal_buffer->drain(internal_buffer->length());
      internal_buffer->addBufferFragment(
          *Buffer::BridgeFragment::createBridgeFragment(result.data));
    } else {
      data.drain(data.length());
      data.addBufferFragment(*Buffer::BridgeFragment::createBridgeFragment(result.data));
    }
    state_.iteration_state_ = IterationState::Ongoing;
    state_.data_forwarded_ = true;
    return Http::FilterDataStatus::Continue;

  default:
    PANIC("invalid filter state: unsupported status for platform filters");
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

Http::FilterTrailersStatus PlatformBridgeFilter::FilterBase::onTrailers(Http::HeaderMap& trailers) {
  ScopeTrackerScopeState scope(&parent_, parent_.scopeTracker());
  state_.stream_complete_ = true;

  // Allow nullptr to act as no-op.
  if (on_trailers_ == nullptr) {
    state_.trailers_forwarded_ = true;
    return Http::FilterTrailersStatus::Continue;
  }

  auto internal_buffer = buffer();
  envoy_headers in_trailers = Http::Utility::toBridgeHeaders(trailers);
  ENVOY_LOG(trace, "PlatformBridgeFilter({})->on_*_trailers", parent_.filter_name_);
  envoy_filter_trailers_status result =
      on_trailers_(in_trailers, streamIntel(), parent_.platform_filter_.instance_context);
  state_.on_trailers_called_ = true;

  switch (result.status) {
  case kEnvoyFilterTrailersStatusContinue:
    RELEASE_ASSERT(state_.iteration_state_ != IterationState::Stopped,
                   "invalid filter state: ResumeIteration may only be used when filter iteration "
                   "is stopped");
    replaceHeaders(trailers, result.trailers);
    state_.trailers_forwarded_ = true;
    return Http::FilterTrailersStatus::Continue;

  case kEnvoyFilterTrailersStatusStopIteration:
    pending_trailers_ = &trailers;
    state_.iteration_state_ = IterationState::Stopped;
    ASSERT(result.trailers.length == 0 && result.trailers.entries == NULL);
    return Http::FilterTrailersStatus::StopIteration;

  // Resume previously-stopped iteration, possibly forwarding headers and data if iteration was
  // stopped during an on*Headers or on*Data invocation.
  case kEnvoyFilterTrailersStatusResumeIteration:
    RELEASE_ASSERT(state_.iteration_state_ == IterationState::Stopped,
                   "invalid filter state: ResumeIteration may only be used when filter iteration "
                   "is stopped");
    // Update pending henders before resuming iteration, if needed.
    if (result.pending_headers) {
      replaceHeaders(*pending_headers_, *result.pending_headers);
      pending_headers_ = nullptr;
      free(result.pending_headers);
    }
    // We've already moved data into the internal buffer and presented it to the platform. Replace
    // the internal buffer with any modifications returned by the platform filter prior to
    // resumption.
    if (result.pending_data) {
      internal_buffer->drain(internal_buffer->length());
      internal_buffer->addBufferFragment(
          *Buffer::BridgeFragment::createBridgeFragment(*result.pending_data));
      free(result.pending_data);
    }
    replaceHeaders(trailers, result.trailers);
    state_.iteration_state_ = IterationState::Ongoing;
    state_.trailers_forwarded_ = true;
    return Http::FilterTrailersStatus::Continue;

  default:
    PANIC("invalid filter state: unsupported status for platform filters");
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

Http::FilterHeadersStatus PlatformBridgeFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool end_stream) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::decodeHeaders(end_stream:{})", filter_name_,
            end_stream);

  // Delegate to base implementation for request and response path.
  return request_filter_base_->onHeaders(headers, end_stream);
}

Http::FilterHeadersStatus PlatformBridgeFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::encodeHeaders(end_stream:{})", filter_name_,
            end_stream);

  // Presence of internal error header indicates an error that should be surfaced as an
  // error callback (rather than an HTTP response).
  const auto error_code_header = headers.get(Http::InternalHeaders::get().ErrorCode);
  if (error_code_header.empty()) {
    // No error, so delegate to base implementation for request and response path.
    return response_filter_base_->onHeaders(headers, end_stream);
  }

  // Update stream state, since we won't be delegating to FilterBase.
  response_filter_base_->state_.stream_complete_ = end_stream;
  error_response_ = true;

  envoy_error_code_t error_code;
  bool parsed_code = absl::SimpleAtoi(error_code_header[0]->value().getStringView(), &error_code);
  RELEASE_ASSERT(parsed_code, "parse error reading error code");

  envoy_data error_message = envoy_nodata;
  const auto error_message_header = headers.get(Http::InternalHeaders::get().ErrorMessage);
  if (!error_message_header.empty()) {
    error_message =
        Data::Utility::copyToBridgeData(error_message_header[0]->value().getStringView());
  }

  int32_t attempt_count = 1;
  if (headers.EnvoyAttemptCount()) {
    bool parsed_attempts =
        absl::SimpleAtoi(headers.EnvoyAttemptCount()->value().getStringView(), &attempt_count);
    RELEASE_ASSERT(parsed_attempts, "parse error reading attempt count");
  }

  if (platform_filter_.on_error) {
    platform_filter_.on_error({error_code, error_message, attempt_count}, streamIntel(),
                              finalStreamIntel(), platform_filter_.instance_context);
  } else {
    release_envoy_data(error_message);
  }

  response_filter_base_->state_.headers_forwarded_ = true;
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus PlatformBridgeFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::decodeData(length:{}, end_stream:{})", filter_name_,
            data.length(), end_stream);

  // Delegate to base implementation for request and response path.
  return request_filter_base_->onData(data, end_stream);
}

Http::FilterDataStatus PlatformBridgeFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::encodeData(length:{}, end_stream:{})", filter_name_,
            data.length(), end_stream);

  // Pass through if already mapped to error response.
  if (error_response_) {
    response_filter_base_->state_.data_forwarded_ = true;
    return Http::FilterDataStatus::Continue;
  }

  // Delegate to base implementation for request and response path.
  return response_filter_base_->onData(data, end_stream);
}

Http::FilterTrailersStatus PlatformBridgeFilter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::decodeTrailers", filter_name_);

  // Delegate to base implementation for request and response path.
  return request_filter_base_->onTrailers(trailers);
}

Http::FilterTrailersStatus
PlatformBridgeFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::encodeTrailers", filter_name_);

  // Pass through if already mapped to error response.
  if (error_response_) {
    response_filter_base_->state_.trailers_forwarded_ = true;
    return Http::FilterTrailersStatus::Continue;
  }

  // Delegate to base implementation for request and response path.
  return response_filter_base_->onTrailers(trailers);
}

void PlatformBridgeFilter::resumeDecoding() {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::resumeDecoding", filter_name_);

  auto weak_self = weak_from_this();
  // TODO(goaway): There's a potential shutdown race here, due to the fact that the shared
  // reference that now holds the filter does not retain the dispatcher. In the future we should
  // make this safer by, e.g.:
  // 1) adding support to Envoy for (optionally) retaining the dispatcher, or
  // 2) retaining the engine to transitively retain the dispatcher via Envoy's ownership graph, or
  // 3) dispatching via a safe intermediary
  // Relevant: https://github.com/envoyproxy/envoy-mobile/issues/332
  dispatcher_.post([weak_self]() -> void {
    if (auto self = weak_self.lock()) {
      // Delegate to base implementation for request and response path.
      self->request_filter_base_->onResume();
    }
  });
}

void PlatformBridgeFilter::resumeEncoding() {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::resumeEncoding", filter_name_);

  auto weak_self = weak_from_this();
  dispatcher_.post([weak_self]() -> void {
    if (auto self = weak_self.lock()) {
      // Delegate to base implementation for request and response path.
      self->response_filter_base_->onResume();
    }
  });
}

void PlatformBridgeFilter::resetIdleTimer() {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::resetIdleTimer", filter_name_);

  auto weak_self = weak_from_this();
  dispatcher_.post([weak_self]() -> void {
    if (auto self = weak_self.lock()) {
      // Stream idle timeout is nondirectional.
      self->decoder_callbacks_->resetIdleTimer();
    }
  });
}

void PlatformBridgeFilter::FilterBase::onResume() {
  ScopeTrackerScopeState scope(&parent_, parent_.scopeTracker());
  ENVOY_LOG(debug, "PlatformBridgeFilter({})::onResume", parent_.filter_name_);
  if (!parent_.isAlive()) {
    return;
  }

  if (state_.iteration_state_ == IterationState::Ongoing) {
    return;
  }

  auto internal_buffer = buffer();
  envoy_headers bridged_headers;
  envoy_data bridged_data;
  envoy_headers bridged_trailers;
  envoy_headers* pending_headers = nullptr;
  envoy_data* pending_data = nullptr;
  envoy_headers* pending_trailers = nullptr;

  if (pending_headers_) {
    bridged_headers = Http::Utility::toBridgeHeaders(*pending_headers_);
    pending_headers = &bridged_headers;
  }
  if (internal_buffer) {
    bridged_data = Data::Utility::copyToBridgeData(*internal_buffer);
    pending_data = &bridged_data;
  }
  if (pending_trailers_) {
    bridged_trailers = Http::Utility::toBridgeHeaders(*pending_trailers_);
    pending_trailers = &bridged_trailers;
  }

  ENVOY_LOG(trace, "PlatformBridgeFilter({})->on_resume_*", parent_.filter_name_);
  envoy_filter_resume_status result =
      on_resume_(pending_headers, pending_data, pending_trailers, state_.stream_complete_,
                 streamIntel(), parent_.platform_filter_.instance_context);
  state_.on_resume_called_ = true;

  if (result.status == kEnvoyFilterResumeStatusStopIteration) {
    RELEASE_ASSERT(!result.pending_headers, "invalid filter state: headers must not be present on "
                                            "stopping filter iteration on async resume");
    RELEASE_ASSERT(!result.pending_data, "invalid filter state: data must not be present on "
                                         "stopping filter iteration on async resume");
    RELEASE_ASSERT(!result.pending_trailers, "invalid filter state: trailers must not be present on"
                                             " stopping filter iteration on async resume");
    return;
  }

  if (pending_headers_) {
    RELEASE_ASSERT(result.pending_headers, "invalid filter state: headers are pending and must be "
                                           "returned to resume filter iteration");
    replaceHeaders(*pending_headers_, *result.pending_headers);
    pending_headers_ = nullptr;
    ENVOY_LOG(debug, "PlatformBridgeFilter({})->on_resume_ process headers free#1",
              parent_.filter_name_);
    if (pending_headers != result.pending_headers) {
      free(result.pending_headers);
    }
  }

  if (internal_buffer) {
    RELEASE_ASSERT(
        result.pending_data,
        "invalid filter state: data is pending and must be returned to resume filter iteration");
    internal_buffer->drain(internal_buffer->length());
    internal_buffer->addBufferFragment(
        *Buffer::BridgeFragment::createBridgeFragment(*result.pending_data));
    ENVOY_LOG(debug, "PlatformBridgeFilter({})->on_resume_ process data free#1",
              parent_.filter_name_);
    if (pending_data != result.pending_data) {
      free(result.pending_data);
    }
  } else if (result.pending_data) {
    addData(*result.pending_data);
    ENVOY_LOG(debug, "PlatformBridgeFilter({})->on_resume_ process data free#2",
              parent_.filter_name_);
    if (pending_data != result.pending_data) {
      free(result.pending_data);
    }
  }

  if (pending_trailers_) {
    RELEASE_ASSERT(result.pending_trailers, "invalid filter state: trailers are pending and must "
                                            "be returned to resume filter iteration");
    replaceHeaders(*pending_trailers_, *result.pending_trailers);
    pending_trailers_ = nullptr;
    ENVOY_LOG(debug, "PlatformBridgeFilter({})->on_resume_ process trailers free#1",
              parent_.filter_name_);
    if (pending_trailers != result.pending_trailers) {
      free(result.pending_trailers);
    }
  } else if (result.pending_trailers) {
    addTrailers(*result.pending_trailers);
    ENVOY_LOG(debug, "PlatformBridgeFilter({})->on_resume_ process trailers free#2",
              parent_.filter_name_);
    if (pending_trailers != result.pending_trailers) {
      free(result.pending_trailers);
    }
  }

  state_.iteration_state_ = IterationState::Ongoing;
  resumeIteration();
}

void PlatformBridgeFilter::FilterBase::dumpState(std::ostream& os, int indent_level) {
  Buffer::Instance* buffer = this->buffer();
  const char* spaces = spacesForLevel(indent_level);
  os << spaces
     << DUMP_MEMBER_AS(state_.iteration_state_,
                       (state_.iteration_state_ == IterationState::Ongoing ? "ongoing" : "stopped"))
     << DUMP_MEMBER(state_.on_headers_called_) << DUMP_MEMBER(state_.headers_forwarded_)
     << DUMP_MEMBER(state_.on_data_called_) << DUMP_MEMBER(state_.data_forwarded_)
     << DUMP_MEMBER(state_.on_trailers_called_) << DUMP_MEMBER(state_.trailers_forwarded_)
     << DUMP_MEMBER(state_.on_resume_called_) << DUMP_NULLABLE_MEMBER(pending_headers_, "pending")
     << DUMP_NULLABLE_MEMBER(buffer, fmt::format("{} bytes", buffer->length()))
     << DUMP_NULLABLE_MEMBER(pending_trailers_, "pending") << DUMP_MEMBER(state_.stream_complete_)
     << std::endl;
};

void PlatformBridgeFilter::RequestFilterBase::addData(envoy_data data) {
  Buffer::OwnedImpl inject_data;
  inject_data.addBufferFragment(*Buffer::BridgeFragment::createBridgeFragment(data));
  parent_.decoder_callbacks_->addDecodedData(inject_data, /* watermark */ false);
}

void PlatformBridgeFilter::ResponseFilterBase::addData(envoy_data data) {
  Buffer::OwnedImpl inject_data;
  inject_data.addBufferFragment(*Buffer::BridgeFragment::createBridgeFragment(data));
  parent_.encoder_callbacks_->addEncodedData(inject_data, /* watermark */ false);
}

void PlatformBridgeFilter::RequestFilterBase::addTrailers(envoy_headers trailers) {
  Http::HeaderMap& inject_trailers = parent_.decoder_callbacks_->addDecodedTrailers();
  replaceHeaders(inject_trailers, trailers);
}

void PlatformBridgeFilter::ResponseFilterBase::addTrailers(envoy_headers trailers) {
  Http::HeaderMap& inject_trailers = parent_.encoder_callbacks_->addEncodedTrailers();
  replaceHeaders(inject_trailers, trailers);
}

void PlatformBridgeFilter::RequestFilterBase::resumeIteration() {
  parent_.decoder_callbacks_->continueDecoding();
}

void PlatformBridgeFilter::ResponseFilterBase::resumeIteration() {
  parent_.encoder_callbacks_->continueEncoding();
}

// Technically-speaking to align with Envoy's internal API this method should take
// a closure to execute with the available buffer, but since we control all usage,
// this shortcut works for now.
Buffer::Instance* PlatformBridgeFilter::RequestFilterBase::buffer() {
  Buffer::Instance* internal_buffer = nullptr;
  // This only exists to provide a mutable buffer, and that buffer is only used when iteration is
  // stopped. We check iteration state here before returning the buffer, to ensure this filter is
  // the one that stopped iteration.
  if (state_.iteration_state_ == IterationState::Stopped &&
      parent_.decoder_callbacks_->decodingBuffer()) {
    parent_.decoder_callbacks_->modifyDecodingBuffer(
        [&internal_buffer](Buffer::Instance& mutable_buffer) {
          internal_buffer = &mutable_buffer;
        });
  }
  return internal_buffer;
}

// Technically-speaking to align with Envoy's internal API this method should take
// a closure to execute with the available buffer, but since we control all usage,
// this shortcut works for now.
Buffer::Instance* PlatformBridgeFilter::ResponseFilterBase::buffer() {
  Buffer::Instance* internal_buffer = nullptr;
  // This only exists to provide a mutable buffer, and that buffer is only used when iteration is
  // stopped. We check iteration state here before returning the buffer, to ensure this filter is
  // the one that stopped iteration.
  if (state_.iteration_state_ == IterationState::Stopped &&
      parent_.encoder_callbacks_->encodingBuffer()) {
    parent_.encoder_callbacks_->modifyEncodingBuffer(
        [&internal_buffer](Buffer::Instance& mutable_buffer) {
          internal_buffer = &mutable_buffer;
        });
  }
  return internal_buffer;
}

} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
