#include "library/common/extensions/filters/http/platform_bridge/filter.h"

#include "envoy/server/filter_config.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/utility.h"

#include "library/common/api/external.h"
#include "library/common/buffer/bridge_fragment.h"
#include "library/common/buffer/utility.h"
#include "library/common/extensions/filters/http/platform_bridge/c_type_definitions.h"
#include "library/common/http/header_utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PlatformBridge {

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

  // If init_filter is missing, zero out the rest of the struct for safety.
  if (platform_filter_.init_filter == nullptr) {
    ENVOY_LOG(debug, "PlatformBridgeFilter({}): missing initializer", filter_name_);
    platform_filter_ = {};
    return;
  }

  // Set the instance_context to the result of the initialization call. Cleanup will ultimately
  // occur during in the onDestroy() invocation below.
  ENVOY_LOG(trace, "PlatformBridgeFilter({})->init_filter", filter_name_);
  platform_filter_.instance_context = platform_filter_.init_filter(platform_filter_.static_context);
  ASSERT(platform_filter_.instance_context,
         fmt::format("PlatformBridgeFilter({}): init_filter unsuccessful", filter_name_));
  iteration_state_ = IterationState::Ongoing;
}

void PlatformBridgeFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::setDecoderCallbacks", filter_name_);
  decoder_callbacks_ = &callbacks;

  if (platform_filter_.set_request_callbacks) {
    platform_request_callbacks_.resume_iteration = envoy_filter_callback_resume_decoding;
    platform_request_callbacks_.release_callbacks = envoy_filter_release_callbacks;
    // We use a weak_ptr wrapper for the filter to ensure presence before dispatching callbacks.
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

  if (platform_filter_.set_response_callbacks) {
    platform_response_callbacks_.resume_iteration = envoy_filter_callback_resume_encoding;
    platform_response_callbacks_.release_callbacks = envoy_filter_release_callbacks;
    // We use a weak_ptr wrapper for the filter to ensure presence before dispatching callbacks.
    platform_response_callbacks_.callback_context =
        new PlatformBridgeFilterWeakPtr{shared_from_this()};
    ENVOY_LOG(trace, "PlatformBridgeFilter({})->set_response_callbacks", filter_name_);
    platform_filter_.set_response_callbacks(platform_response_callbacks_,
                                            platform_filter_.instance_context);
  }
}

void PlatformBridgeFilter::onDestroy() {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::onDestroy", filter_name_);
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

void PlatformBridgeFilter::replaceHeaders(Http::HeaderMap& headers, envoy_headers c_headers) {
  headers.clear();
  for (envoy_header_size_t i = 0; i < c_headers.length; i++) {
    headers.addCopy(Http::LowerCaseString(Http::Utility::convertToString(c_headers.headers[i].key)),
                    Http::Utility::convertToString(c_headers.headers[i].value));
  }
  // The C envoy_headers struct can be released now because the headers have been copied.
  release_envoy_headers(c_headers);
}

Http::FilterHeadersStatus PlatformBridgeFilter::onHeaders(Http::HeaderMap& headers, bool end_stream,
                                                          envoy_filter_on_headers_f on_headers) {
  // Allow nullptr to act as no-op.
  if (on_headers == nullptr) {
    return Http::FilterHeadersStatus::Continue;
  }

  envoy_headers in_headers = Http::Utility::toBridgeHeaders(headers);
  ENVOY_LOG(trace, "PlatformBridgeFilter({})->on_*_headers", filter_name_);
  envoy_filter_headers_status result =
      on_headers(in_headers, end_stream, platform_filter_.instance_context);

  switch (result.status) {
  case kEnvoyFilterHeadersStatusContinue:
    PlatformBridgeFilter::replaceHeaders(headers, result.headers);
    return Http::FilterHeadersStatus::Continue;

  case kEnvoyFilterHeadersStatusStopIteration:
    iteration_state_ = IterationState::Stopped;
    return Http::FilterHeadersStatus::StopIteration;

  default:
    PANIC("invalid filter state: unsupported status for platform filters");
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

Http::FilterDataStatus PlatformBridgeFilter::onData(Buffer::Instance& data, bool end_stream,
                                                    Buffer::Instance* internal_buffer,
                                                    Http::HeaderMap** pending_headers,
                                                    envoy_filter_on_data_f on_data) {
  // Allow nullptr to act as no-op.
  if (on_data == nullptr) {
    return Http::FilterDataStatus::Continue;
  }

  envoy_data in_data;
  bool already_buffering = iteration_state_ == IterationState::Stopped && internal_buffer &&
                           internal_buffer->length() > 0;

  if (already_buffering) {
    // Pre-emptively buffer data to present aggregate to platform.
    internal_buffer->move(data);
    in_data = Buffer::Utility::copyToBridgeData(*internal_buffer);
  } else {
    in_data = Buffer::Utility::copyToBridgeData(data);
  }

  ENVOY_LOG(trace, "PlatformBridgeFilter({})->on_*_data", filter_name_);
  envoy_filter_data_status result = on_data(in_data, end_stream, platform_filter_.instance_context);

  switch (result.status) {
  case kEnvoyFilterDataStatusContinue:
    RELEASE_ASSERT(iteration_state_ != IterationState::Stopped,
                   "invalid filter state: filter iteration must be resumed with ResumeIteration");
    data.drain(data.length());
    data.addBufferFragment(*Buffer::BridgeFragment::createBridgeFragment(result.data));
    return Http::FilterDataStatus::Continue;

  case kEnvoyFilterDataStatusStopIterationAndBuffer:
    if (already_buffering) {
      // Data will already have been added to the internal buffer (above).
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    // Data will be buffered on return.
    iteration_state_ = IterationState::Stopped;
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
    iteration_state_ = IterationState::Stopped;
    return Http::FilterDataStatus::StopIterationNoBuffer;

  // Resume previously-stopped iteration, possibly forwarding headers if iteration was stopped
  // during an on*Headers invocation.
  case kEnvoyFilterDataStatusResumeIteration:
    RELEASE_ASSERT(iteration_state_ == IterationState::Stopped,
                   "invalid filter state: ResumeIteration may only be used when filter iteration "
                   "is stopped");
    // Update pending henders before resuming iteration, if needed.
    if (result.pending_headers) {
      PlatformBridgeFilter::replaceHeaders(**pending_headers, *result.pending_headers);
      *pending_headers = nullptr;
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
    iteration_state_ = IterationState::Ongoing;
    return Http::FilterDataStatus::Continue;

  default:
    PANIC("invalid filter state: unsupported status for platform filters");
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

Http::FilterTrailersStatus
PlatformBridgeFilter::onTrailers(Http::HeaderMap& trailers, Buffer::Instance* internal_buffer,
                                 Http::HeaderMap** pending_headers,
                                 envoy_filter_on_trailers_f on_trailers) {
  // Allow nullptr to act as no-op.
  if (on_trailers == nullptr) {
    return Http::FilterTrailersStatus::Continue;
  }

  envoy_headers in_trailers = Http::Utility::toBridgeHeaders(trailers);
  ENVOY_LOG(trace, "PlatformBridgeFilter({})->on_*_trailers", filter_name_);
  envoy_filter_trailers_status result = on_trailers(in_trailers, platform_filter_.instance_context);

  switch (result.status) {
  case kEnvoyFilterTrailersStatusContinue:
    RELEASE_ASSERT(iteration_state_ != IterationState::Stopped,
                   "invalid filter state: ResumeIteration may only be used when filter iteration "
                   "is stopped");
    PlatformBridgeFilter::replaceHeaders(trailers, result.trailers);
    return Http::FilterTrailersStatus::Continue;

  case kEnvoyFilterTrailersStatusStopIteration:
    iteration_state_ = IterationState::Stopped;
    return Http::FilterTrailersStatus::StopIteration;

  // Resume previously-stopped iteration, possibly forwarding headers and data if iteration was
  // stopped during an on*Headers or on*Data invocation.
  case kEnvoyFilterTrailersStatusResumeIteration:
    RELEASE_ASSERT(iteration_state_ == IterationState::Stopped,
                   "invalid filter state: ResumeIteration may only be used when filter iteration "
                   "is stopped");
    // Update pending henders before resuming iteration, if needed.
    if (result.pending_headers) {
      PlatformBridgeFilter::replaceHeaders(**pending_headers, *result.pending_headers);
      *pending_headers = nullptr;
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
    PlatformBridgeFilter::replaceHeaders(trailers, result.trailers);
    iteration_state_ = IterationState::Ongoing;
    return Http::FilterTrailersStatus::Continue;

  default:
    PANIC("invalid filter state: unsupported status for platform filters");
  }

  NOT_REACHED_GCOVR_EXCL_LINE;
}

Http::FilterHeadersStatus PlatformBridgeFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                              bool end_stream) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::decodeHeaders", filter_name_);

  // Delegate to shared implementation for request and response path.
  auto status = onHeaders(headers, end_stream, platform_filter_.on_request_headers);
  if (status == Http::FilterHeadersStatus::StopIteration) {
    pending_request_headers_ = &headers;
  }
  request_complete_ = end_stream;
  return status;
}

Http::FilterHeadersStatus PlatformBridgeFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                              bool end_stream) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::encodeHeaders", filter_name_);

  // Delegate to shared implementation for request and response path.
  auto status = onHeaders(headers, end_stream, platform_filter_.on_response_headers);
  if (status == Http::FilterHeadersStatus::StopIteration) {
    pending_response_headers_ = &headers;
  }
  response_complete_ = end_stream;
  return status;
}

Http::FilterDataStatus PlatformBridgeFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::decodeData", filter_name_);

  // Delegate to shared implementation for request and response path.
  Buffer::Instance* internal_buffer = nullptr;
  if (decoder_callbacks_->decodingBuffer()) {
    decoder_callbacks_->modifyDecodingBuffer([&internal_buffer](Buffer::Instance& mutable_buffer) {
      internal_buffer = &mutable_buffer;
    });
  }

  auto status = onData(data, end_stream, internal_buffer, &pending_request_headers_,
                       platform_filter_.on_request_data);
  request_complete_ = end_stream;
  return status;
}

Http::FilterDataStatus PlatformBridgeFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::encodeData", filter_name_);

  // Delegate to shared implementation for request and response path.
  Buffer::Instance* internal_buffer = nullptr;
  if (encoder_callbacks_->encodingBuffer()) {
    encoder_callbacks_->modifyEncodingBuffer([&internal_buffer](Buffer::Instance& mutable_buffer) {
      internal_buffer = &mutable_buffer;
    });
  }

  auto status = onData(data, end_stream, internal_buffer, &pending_response_headers_,
                       platform_filter_.on_response_data);
  response_complete_ = end_stream;
  return status;
}

Http::FilterTrailersStatus PlatformBridgeFilter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::decodeTrailers", filter_name_);

  // Delegate to shared implementation for request and response path.
  Buffer::Instance* internal_buffer = nullptr;
  if (decoder_callbacks_->decodingBuffer()) {
    decoder_callbacks_->modifyDecodingBuffer([&internal_buffer](Buffer::Instance& mutable_buffer) {
      internal_buffer = &mutable_buffer;
    });
  }

  auto status = onTrailers(trailers, internal_buffer, &pending_request_headers_,
                           platform_filter_.on_request_trailers);
  if (status == Http::FilterTrailersStatus::StopIteration) {
    pending_request_trailers_ = &trailers;
  }
  request_complete_ = true;
  return status;
}

Http::FilterTrailersStatus
PlatformBridgeFilter::encodeTrailers(Http::ResponseTrailerMap& trailers) {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::encodeTrailers", filter_name_);

  // Delegate to shared implementation for request and response path.
  Buffer::Instance* internal_buffer = nullptr;
  if (encoder_callbacks_->encodingBuffer()) {
    encoder_callbacks_->modifyEncodingBuffer([&internal_buffer](Buffer::Instance& mutable_buffer) {
      internal_buffer = &mutable_buffer;
    });
  }

  auto status = onTrailers(trailers, internal_buffer, &pending_response_headers_,
                           platform_filter_.on_response_trailers);
  if (status == Http::FilterTrailersStatus::StopIteration) {
    pending_response_trailers_ = &trailers;
  }
  response_complete_ = true;
  return status;
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
  // Relevant: https://github.com/lyft/envoy-mobile/issues/332
  dispatcher_.post([weak_self]() -> void {
    if (auto self = weak_self.lock()) {
      self->onResumeDecoding();
    }
  });
}

void PlatformBridgeFilter::onResumeDecoding() {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::onResumeDecoding", filter_name_);

  if (iteration_state_ == IterationState::Ongoing) {
    return;
  }

  Buffer::Instance* internal_buffer = nullptr;
  if (decoder_callbacks_->decodingBuffer()) {
    decoder_callbacks_->modifyDecodingBuffer([&internal_buffer](Buffer::Instance& mutable_buffer) {
      internal_buffer = &mutable_buffer;
    });
  }

  envoy_headers bridged_headers;
  envoy_data bridged_data;
  envoy_headers bridged_trailers;
  envoy_headers* pending_headers = nullptr;
  envoy_data* pending_data = nullptr;
  envoy_headers* pending_trailers = nullptr;

  if (pending_request_headers_) {
    bridged_headers = Http::Utility::toBridgeHeaders(*pending_request_headers_);
    pending_headers = &bridged_headers;
  }
  if (internal_buffer) {
    bridged_data = Buffer::Utility::copyToBridgeData(*internal_buffer);
    pending_data = &bridged_data;
  }
  if (pending_request_trailers_) {
    bridged_trailers = Http::Utility::toBridgeHeaders(*pending_request_trailers_);
    pending_trailers = &bridged_trailers;
  }

  ENVOY_LOG(trace, "PlatformBridgeFilter({})->on_resume_request", filter_name_);
  envoy_filter_resume_status result =
      platform_filter_.on_resume_request(pending_headers, pending_data, pending_trailers,
                                         request_complete_, platform_filter_.instance_context);
  if (result.status == kEnvoyFilterResumeStatusStopIteration) {
    return;
  }
  if (pending_request_headers_) {
    RELEASE_ASSERT(result.pending_headers, "invalid filter state: headers are pending and must be "
                                           "returned to resume filter iteration");
    PlatformBridgeFilter::replaceHeaders(*pending_request_headers_, *result.pending_headers);
    pending_request_headers_ = nullptr;
    free(result.pending_headers);
  }
  if (internal_buffer) {
    RELEASE_ASSERT(
        result.pending_data,
        "invalid filter state: data is pending and must be returned to resume filter iteration");
    internal_buffer->drain(internal_buffer->length());
    internal_buffer->addBufferFragment(
        *Buffer::BridgeFragment::createBridgeFragment(*result.pending_data));
    free(result.pending_data);
  } else if (result.pending_data) {
    Buffer::OwnedImpl inject_data;
    inject_data.addBufferFragment(
        *Buffer::BridgeFragment::createBridgeFragment(*result.pending_data));
    decoder_callbacks_->addDecodedData(inject_data, /* watermark */ false);
    free(result.pending_data);
  }
  if (pending_request_trailers_) {
    RELEASE_ASSERT(result.pending_trailers, "invalid filter state: trailers are pending and must "
                                            "be returned to resume filter iteration");
    PlatformBridgeFilter::replaceHeaders(*pending_request_trailers_, *result.pending_trailers);
    pending_request_trailers_ = nullptr;
    free(result.pending_trailers);
  }
  iteration_state_ = IterationState::Ongoing;
  decoder_callbacks_->continueDecoding();
}

void PlatformBridgeFilter::resumeEncoding() {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::resumeEncoding", filter_name_);

  auto weak_self = weak_from_this();
  dispatcher_.post([weak_self]() -> void {
    if (auto self = weak_self.lock()) {
      self->onResumeEncoding();
    }
  });
}

void PlatformBridgeFilter::onResumeEncoding() {
  ENVOY_LOG(trace, "PlatformBridgeFilter({})::onResumeEncoding", filter_name_);

  if (iteration_state_ == IterationState::Ongoing) {
    return;
  }

  Buffer::Instance* internal_buffer = nullptr;
  if (encoder_callbacks_->encodingBuffer()) {
    encoder_callbacks_->modifyEncodingBuffer([&internal_buffer](Buffer::Instance& mutable_buffer) {
      internal_buffer = &mutable_buffer;
    });
  }

  envoy_headers bridged_headers;
  envoy_data bridged_data;
  envoy_headers bridged_trailers;
  envoy_headers* pending_headers = nullptr;
  envoy_data* pending_data = nullptr;
  envoy_headers* pending_trailers = nullptr;

  if (pending_response_headers_) {
    bridged_headers = Http::Utility::toBridgeHeaders(*pending_response_headers_);
    pending_headers = &bridged_headers;
  }
  if (internal_buffer) {
    bridged_data = Buffer::Utility::copyToBridgeData(*internal_buffer);
    pending_data = &bridged_data;
  }
  if (pending_response_trailers_) {
    bridged_trailers = Http::Utility::toBridgeHeaders(*pending_response_trailers_);
    pending_trailers = &bridged_trailers;
  }

  ENVOY_LOG(trace, "PlatformBridgeFilter({})->on_resume_response", filter_name_);
  envoy_filter_resume_status result =
      platform_filter_.on_resume_response(pending_headers, pending_data, pending_trailers,
                                          response_complete_, platform_filter_.instance_context);
  if (pending_response_headers_) {
    RELEASE_ASSERT(result.pending_headers, "invalid filter state: headers are pending and must be "
                                           "returned to resume filter iteration");
    PlatformBridgeFilter::replaceHeaders(*pending_response_headers_, *result.pending_headers);
    pending_response_headers_ = nullptr;
    free(result.pending_headers);
  }
  if (internal_buffer) {
    RELEASE_ASSERT(
        result.pending_data,
        "invalid filter state: data is pending and must be returned to resume filter iteration");
    internal_buffer->drain(internal_buffer->length());
    internal_buffer->addBufferFragment(
        *Buffer::BridgeFragment::createBridgeFragment(*result.pending_data));
    free(result.pending_data);
  } else if (result.pending_data) {
    Buffer::OwnedImpl inject_data;
    inject_data.addBufferFragment(
        *Buffer::BridgeFragment::createBridgeFragment(*result.pending_data));
    encoder_callbacks_->addEncodedData(inject_data, /* watermark */ false);
    free(result.pending_data);
  }
  if (pending_response_trailers_) {
    RELEASE_ASSERT(result.pending_trailers, "invalid filter state: trailers are pending and must "
                                            "be returned to resume filter iteration");
    PlatformBridgeFilter::replaceHeaders(*pending_response_trailers_, *result.pending_trailers);
    pending_response_trailers_ = nullptr;
    free(result.pending_trailers);
  }
  iteration_state_ = IterationState::Ongoing;
  encoder_callbacks_->continueEncoding();
}

} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
