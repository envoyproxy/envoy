#include "source/common/http/filter_manager.h"

#include <functional>

#include "envoy/http/header_map.h"
#include "envoy/matcher/matcher.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/scope_tracked_object_stack.h"
#include "source/common/common/scope_tracker.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"

#include "matching/data_impl.h"

namespace Envoy {
namespace Http {

namespace {

template <class T> using FilterList = std::list<std::unique_ptr<T>>;

// Shared helper for recording the latest filter used.
template <class T>
void recordLatestDataFilter(const typename FilterList<T>::iterator current_filter,
                            T*& latest_filter, const FilterList<T>& filters) {
  // If this is the first time we're calling onData, just record the current filter.
  if (latest_filter == nullptr) {
    latest_filter = current_filter->get();
    return;
  }

  // We want to keep this pointing at the latest filter in the filter list that has received the
  // onData callback. To do so, we compare the current latest with the *previous* filter. If they
  // match, then we must be processing a new filter for the first time. We omit this check if we're
  // the first filter, since the above check handles that case.
  //
  // We compare against the previous filter to avoid multiple filter iterations from resetting the
  // pointer: If we just set latest to current, then the first onData filter iteration would
  // correctly iterate over the filters and set latest, but on subsequent onData iterations
  // we'd start from the beginning again, potentially allowing filter N to modify the buffer even
  // though filter M > N was the filter that inserted data into the buffer.
  if (current_filter != filters.begin() && latest_filter == std::prev(current_filter)->get()) {
    latest_filter = current_filter->get();
  }
}

} // namespace

void ActiveStreamFilterBase::commonContinue() {
  if (!canContinue()) {
    ENVOY_STREAM_LOG(trace, "cannot continue filter chain: filter={}", *this,
                     static_cast<const void*>(this));
    return;
  }

  // Set ScopeTrackerScopeState if there's no existing crash context.
  ScopeTrackedObjectStack encapsulated_object;
  absl::optional<ScopeTrackerScopeState> state;
  if (parent_.dispatcher_.trackedObjectStackIsEmpty()) {
    restoreContextOnContinue(encapsulated_object);
    state.emplace(&encapsulated_object, parent_.dispatcher_);
  }

  ENVOY_STREAM_LOG(trace, "continuing filter chain: filter={}", *this,
                   static_cast<const void*>(this));
  ASSERT(!canIterate(),
         "Attempting to continue iteration while the IterationState is already Continue");
  // If iteration has stopped for all frame types, set iterate_from_current_filter_ to true so the
  // filter iteration starts with the current filter instead of the next one.
  if (stoppedAll()) {
    iterate_from_current_filter_ = true;
  }
  allowIteration();

  // Only resume with do1xxHeaders() if we've actually seen 1xx headers.
  if (has1xxHeaders()) {
    continued_1xx_headers_ = true;
    do1xxHeaders();
    // If the response headers have not yet come in, don't continue on with
    // headers and body. doHeaders expects request headers to exist.
    if (!parent_.filter_manager_callbacks_.responseHeaders()) {
      return;
    }
  }

  // Make sure that we handle the zero byte data frame case. We make no effort to optimize this
  // case in terms of merging it into a header only request/response. This could be done in the
  // future.
  if (!headers_continued_) {
    headers_continued_ = true;
    doHeaders(complete() && !bufferedData() && !hasTrailers());
  }

  doMetadata();

  // It is possible for trailers to be added during doData(). doData() itself handles continuation
  // of trailers for the non-continuation case. Thus, we must keep track of whether we had
  // trailers prior to calling doData(). If we do, then we continue them here, otherwise we rely
  // on doData() to do so.
  const bool had_trailers_before_data = hasTrailers();
  if (bufferedData()) {
    doData(complete() && !had_trailers_before_data);
  }

  if (had_trailers_before_data) {
    doTrailers();
  }

  iterate_from_current_filter_ = false;
}

bool ActiveStreamFilterBase::commonHandleAfter1xxHeadersCallback(FilterHeadersStatus status) {
  ASSERT(parent_.state_.has_1xx_headers_);
  ASSERT(!continued_1xx_headers_);
  ASSERT(canIterate());

  if (status == FilterHeadersStatus::StopIteration) {
    iteration_state_ = IterationState::StopSingleIteration;
    return false;
  } else {
    ASSERT(status == FilterHeadersStatus::Continue);
    continued_1xx_headers_ = true;
    return true;
  }
}

bool ActiveStreamFilterBase::commonHandleAfterHeadersCallback(FilterHeadersStatus status,
                                                              bool& end_stream) {
  ASSERT(!headers_continued_);
  ASSERT(canIterate());

  switch (status) {
  case FilterHeadersStatus::StopIteration:
    iteration_state_ = IterationState::StopSingleIteration;
    break;
  case FilterHeadersStatus::StopAllIterationAndBuffer:
    iteration_state_ = IterationState::StopAllBuffer;
    break;
  case FilterHeadersStatus::StopAllIterationAndWatermark:
    iteration_state_ = IterationState::StopAllWatermark;
    break;
  case FilterHeadersStatus::ContinueAndDontEndStream:
    end_stream = false;
    headers_continued_ = true;
    ENVOY_STREAM_LOG(debug, "converting to headers and body (body not available yet)", parent_);
    break;
  case FilterHeadersStatus::Continue:
    headers_continued_ = true;
    break;
  }

  handleMetadataAfterHeadersCallback();

  if (stoppedAll() || status == FilterHeadersStatus::StopIteration) {
    return false;
  } else {
    return true;
  }
}

void ActiveStreamFilterBase::commonHandleBufferData(Buffer::Instance& provided_data) {

  // The way we do buffering is a little complicated which is why we have this common function
  // which is used for both encoding and decoding. When data first comes into our filter pipeline,
  // we send it through. Any filter can choose to stop iteration and buffer or not. If we then
  // continue iteration in the future, we use the buffered data. A future filter can stop and
  // buffer again. In this case, since we are already operating on buffered data, we don't
  // rebuffer, because we assume the filter has modified the buffer as it wishes in place.
  if (bufferedData().get() != &provided_data) {
    if (!bufferedData()) {
      bufferedData() = createBuffer();
    }
    bufferedData()->move(provided_data);
  }
}

bool ActiveStreamFilterBase::commonHandleAfterDataCallback(FilterDataStatus status,
                                                           Buffer::Instance& provided_data,
                                                           bool& buffer_was_streaming) {

  if (status == FilterDataStatus::Continue) {
    if (iteration_state_ == IterationState::StopSingleIteration) {
      commonHandleBufferData(provided_data);
      commonContinue();
      return false;
    } else {
      ASSERT(headers_continued_);
    }
  } else {
    iteration_state_ = IterationState::StopSingleIteration;
    if (status == FilterDataStatus::StopIterationAndBuffer ||
        status == FilterDataStatus::StopIterationAndWatermark) {
      buffer_was_streaming = status == FilterDataStatus::StopIterationAndWatermark;
      commonHandleBufferData(provided_data);
    } else if (complete() && !hasTrailers() && !bufferedData() &&
               // If the stream is destroyed, no need to handle the data buffer or trailers.
               // This can occur if the filter calls sendLocalReply.
               !parent_.state_.destroyed_) {
      // If this filter is doing StopIterationNoBuffer and this stream is terminated with a zero
      // byte data frame, we need to create an empty buffer to make sure that when commonContinue
      // is called, the pipeline resumes with an empty data frame with end_stream = true
      ASSERT(end_stream_);
      bufferedData() = createBuffer();
    }

    return false;
  }

  return true;
}

bool ActiveStreamFilterBase::commonHandleAfterTrailersCallback(FilterTrailersStatus status) {

  if (status == FilterTrailersStatus::Continue) {
    if (iteration_state_ == IterationState::StopSingleIteration) {
      commonContinue();
      return false;
    } else {
      ASSERT(headers_continued_);
    }
  } else if (status == FilterTrailersStatus::StopIteration) {
    if (canIterate()) {
      iteration_state_ = IterationState::StopSingleIteration;
    }
    return false;
  }

  return true;
}

OptRef<const Network::Connection> ActiveStreamFilterBase::connection() {
  return parent_.connection();
}

Event::Dispatcher& ActiveStreamFilterBase::dispatcher() { return parent_.dispatcher_; }

StreamInfo::StreamInfo& ActiveStreamFilterBase::streamInfo() { return parent_.streamInfo(); }

Tracing::Span& ActiveStreamFilterBase::activeSpan() {
  return parent_.filter_manager_callbacks_.activeSpan();
}

const ScopeTrackedObject& ActiveStreamFilterBase::scope() {
  return parent_.filter_manager_callbacks_.scope();
}

void ActiveStreamFilterBase::restoreContextOnContinue(
    ScopeTrackedObjectStack& tracked_object_stack) {
  parent_.contextOnContinue(tracked_object_stack);
}

OptRef<const Tracing::Config> ActiveStreamFilterBase::tracingConfig() const {
  return parent_.filter_manager_callbacks_.tracingConfig();
}

Upstream::ClusterInfoConstSharedPtr ActiveStreamFilterBase::clusterInfo() {
  return parent_.filter_manager_callbacks_.clusterInfo();
}

Router::RouteConstSharedPtr ActiveStreamFilterBase::route() { return getRoute(); }

Router::RouteConstSharedPtr ActiveStreamFilterBase::getRoute() const {
  if (parent_.filter_manager_callbacks_.downstreamCallbacks()) {
    return parent_.filter_manager_callbacks_.downstreamCallbacks()->route(nullptr);
  }
  return parent_.streamInfo().route();
}

void ActiveStreamFilterBase::resetIdleTimer() {
  parent_.filter_manager_callbacks_.resetIdleTimer();
}

const Router::RouteSpecificFilterConfig*
ActiveStreamFilterBase::mostSpecificPerFilterConfig() const {
  auto current_route = getRoute();
  if (current_route == nullptr) {
    return nullptr;
  }

  auto* result = current_route->mostSpecificPerFilterConfig(filter_context_.config_name);

  if (result == nullptr && filter_context_.filter_name != filter_context_.config_name) {
    // Fallback to use filter name.
    result = current_route->mostSpecificPerFilterConfig(filter_context_.filter_name);
  }
  return result;
}

void ActiveStreamFilterBase::traversePerFilterConfig(
    std::function<void(const Router::RouteSpecificFilterConfig&)> cb) const {
  Router::RouteConstSharedPtr current_route = getRoute();
  if (current_route == nullptr) {
    return;
  }

  bool handled = false;
  current_route->traversePerFilterConfig(
      filter_context_.config_name,
      [&handled, &cb](const Router::RouteSpecificFilterConfig& config) {
        handled = true;
        cb(config);
      });

  if (handled || filter_context_.filter_name == filter_context_.config_name) {
    return;
  }

  current_route->traversePerFilterConfig(filter_context_.filter_name, cb);
}

Http1StreamEncoderOptionsOptRef ActiveStreamFilterBase::http1StreamEncoderOptions() {
  // TODO(mattklein123): At some point we might want to actually wrap this interface but for now
  // we give the filter direct access to the encoder options.
  return parent_.filter_manager_callbacks_.http1StreamEncoderOptions();
}

OptRef<DownstreamStreamFilterCallbacks> ActiveStreamFilterBase::downstreamCallbacks() {
  return parent_.filter_manager_callbacks_.downstreamCallbacks();
}

OptRef<UpstreamStreamFilterCallbacks> ActiveStreamFilterBase::upstreamCallbacks() {
  return parent_.filter_manager_callbacks_.upstreamCallbacks();
}

bool ActiveStreamDecoderFilter::canContinue() {
  // It is possible for the connection manager to respond directly to a request even while
  // a filter is trying to continue. If a response has already happened, we should not
  // continue to further filters. A concrete example of this is a filter buffering data, the
  // last data frame comes in and the filter continues, but the final buffering takes the stream
  // over the high watermark such that a 413 is returned.
  return !parent_.state_.local_complete_;
}

bool ActiveStreamEncoderFilter::canContinue() {
  // As with ActiveStreamDecoderFilter::canContinue() make sure we do not
  // continue if a local reply has been sent.
  return !parent_.state_.remote_encode_complete_;
}

Buffer::InstancePtr ActiveStreamDecoderFilter::createBuffer() {
  auto buffer = dispatcher().getWatermarkFactory().createBuffer(
      [this]() -> void { this->requestDataDrained(); },
      [this]() -> void { this->requestDataTooLarge(); },
      []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
  buffer->setWatermarks(parent_.buffer_limit_);
  return buffer;
}

Buffer::InstancePtr& ActiveStreamDecoderFilter::bufferedData() {
  return parent_.buffered_request_data_;
}

bool ActiveStreamDecoderFilter::complete() { return parent_.remoteDecodeComplete(); }

void ActiveStreamDecoderFilter::doHeaders(bool end_stream) {
  parent_.decodeHeaders(this, *parent_.filter_manager_callbacks_.requestHeaders(), end_stream);
}

void ActiveStreamDecoderFilter::doData(bool end_stream) {
  parent_.decodeData(this, *parent_.buffered_request_data_, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}

void ActiveStreamDecoderFilter::doTrailers() {
  parent_.decodeTrailers(this, *parent_.filter_manager_callbacks_.requestTrailers());
}
bool ActiveStreamDecoderFilter::hasTrailers() {
  return parent_.filter_manager_callbacks_.requestTrailers().has_value();
}

void ActiveStreamDecoderFilter::drainSavedRequestMetadata() {
  ASSERT(saved_request_metadata_ != nullptr);
  for (auto& metadata_map : *getSavedRequestMetadata()) {
    parent_.decodeMetadata(this, *metadata_map);
  }
  getSavedRequestMetadata()->clear();
}

void ActiveStreamDecoderFilter::handleMetadataAfterHeadersCallback() {
  // If we drain accumulated metadata, the iteration must start with the current filter.
  const bool saved_state = iterate_from_current_filter_;
  iterate_from_current_filter_ = true;
  // If decodeHeaders() returns StopAllIteration, we should skip draining metadata, and wait
  // for doMetadata() to drain the metadata after iteration continues.
  if (!stoppedAll() && saved_request_metadata_ != nullptr && !getSavedRequestMetadata()->empty()) {
    drainSavedRequestMetadata();
  }
  // Restores the original value of iterate_from_current_filter_.
  iterate_from_current_filter_ = saved_state;
}

RequestTrailerMap& ActiveStreamDecoderFilter::addDecodedTrailers() {
  return parent_.addDecodedTrailers();
}

void ActiveStreamDecoderFilter::addDecodedData(Buffer::Instance& data, bool streaming) {
  parent_.addDecodedData(*this, data, streaming);
}

MetadataMapVector& ActiveStreamDecoderFilter::addDecodedMetadata() {
  return parent_.addDecodedMetadata();
}

void ActiveStreamDecoderFilter::injectDecodedDataToFilterChain(Buffer::Instance& data,
                                                               bool end_stream) {
  if (!headers_continued_) {
    headers_continued_ = true;
    doHeaders(false);
  }
  parent_.decodeData(this, data, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}

void ActiveStreamDecoderFilter::continueDecoding() { commonContinue(); }
const Buffer::Instance* ActiveStreamDecoderFilter::decodingBuffer() {
  return parent_.buffered_request_data_.get();
}

void ActiveStreamDecoderFilter::modifyDecodingBuffer(
    std::function<void(Buffer::Instance&)> callback) {
  ASSERT(parent_.state_.latest_data_decoding_filter_ == this);
  callback(*parent_.buffered_request_data_.get());
}

void ActiveStreamDecoderFilter::sendLocalReply(
    Code code, absl::string_view body,
    std::function<void(ResponseHeaderMap& headers)> modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  parent_.sendLocalReply(code, body, modify_headers, grpc_status, details);
}

void ActiveStreamDecoderFilter::encode1xxHeaders(ResponseHeaderMapPtr&& headers) {
  // If Envoy is not configured to proxy 100-Continue responses, swallow the 100 Continue
  // here. This avoids the potential situation where Envoy strips Expect: 100-Continue and sends a
  // 100-Continue, then proxies a duplicate 100 Continue from upstream.
  if (parent_.proxy_100_continue_) {
    parent_.filter_manager_callbacks_.setInformationalHeaders(std::move(headers));
    parent_.encode1xxHeaders(nullptr, *parent_.filter_manager_callbacks_.informationalHeaders());
  }
}

ResponseHeaderMapOptRef ActiveStreamDecoderFilter::informationalHeaders() const {
  return parent_.filter_manager_callbacks_.informationalHeaders();
}

void ActiveStreamDecoderFilter::encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream,
                                              absl::string_view details) {
  parent_.streamInfo().setResponseCodeDetails(details);
  parent_.filter_manager_callbacks_.setResponseHeaders(std::move(headers));
  parent_.encodeHeaders(nullptr, *parent_.filter_manager_callbacks_.responseHeaders(), end_stream);
}

ResponseHeaderMapOptRef ActiveStreamDecoderFilter::responseHeaders() const {
  return parent_.filter_manager_callbacks_.responseHeaders();
}

void ActiveStreamDecoderFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  parent_.encodeData(nullptr, data, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}

void ActiveStreamDecoderFilter::encodeTrailers(ResponseTrailerMapPtr&& trailers) {
  parent_.filter_manager_callbacks_.setResponseTrailers(std::move(trailers));
  parent_.encodeTrailers(nullptr, *parent_.filter_manager_callbacks_.responseTrailers());
}

ResponseTrailerMapOptRef ActiveStreamDecoderFilter::responseTrailers() const {
  return parent_.filter_manager_callbacks_.responseTrailers();
}

void ActiveStreamDecoderFilter::encodeMetadata(MetadataMapPtr&& metadata_map_ptr) {
  parent_.encodeMetadata(nullptr, std::move(metadata_map_ptr));
}

void ActiveStreamDecoderFilter::onDecoderFilterAboveWriteBufferHighWatermark() {
  parent_.filter_manager_callbacks_.onDecoderFilterAboveWriteBufferHighWatermark();
}

void ActiveStreamDecoderFilter::requestDataTooLarge() {
  ENVOY_STREAM_LOG(debug, "request data too large watermark exceeded", parent_);
  if (parent_.state_.decoder_filters_streaming_) {
    onDecoderFilterAboveWriteBufferHighWatermark();
  } else {
    parent_.filter_manager_callbacks_.onRequestDataTooLarge();
    sendLocalReply(Code::PayloadTooLarge, CodeUtility::toString(Code::PayloadTooLarge), nullptr,
                   absl::nullopt, StreamInfo::ResponseCodeDetails::get().RequestPayloadTooLarge);
  }
}

void FilterManager::applyFilterFactoryCb(FilterContext context, FilterFactoryCb& factory) {
  FilterChainFactoryCallbacksImpl callbacks(*this, context);
  factory(callbacks);
}

void FilterManager::maybeContinueDecoding(
    const std::list<ActiveStreamDecoderFilterPtr>::iterator& continue_data_entry) {
  if (continue_data_entry != decoder_filters_.end()) {
    // We use the continueDecoding() code since it will correctly handle not calling
    // decodeHeaders() again. Fake setting StopSingleIteration since the continueDecoding() code
    // expects it.
    ASSERT(buffered_request_data_);
    (*continue_data_entry)->iteration_state_ =
        ActiveStreamFilterBase::IterationState::StopSingleIteration;
    (*continue_data_entry)->continueDecoding();
  }
}

void FilterManager::decodeHeaders(ActiveStreamDecoderFilter* filter, RequestHeaderMap& headers,
                                  bool end_stream) {
  // Headers filter iteration should always start with the next filter if available.
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry =
      commonDecodePrefix(filter, FilterIterationStartState::AlwaysStartFromNext);
  std::list<ActiveStreamDecoderFilterPtr>::iterator continue_data_entry = decoder_filters_.end();

  for (; entry != decoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::DecodeHeaders));
    state_.filter_call_state_ |= FilterCallState::DecodeHeaders;
    (*entry)->end_stream_ = (end_stream && continue_data_entry == decoder_filters_.end());
    FilterHeadersStatus status = (*entry)->decodeHeaders(headers, (*entry)->end_stream_);
    if (state_.decoder_filter_chain_aborted_) {
      ENVOY_STREAM_LOG(trace,
                       "decodeHeaders filter iteration aborted due to local reply: filter={}",
                       *this, (*entry)->filter_context_.config_name);
      status = FilterHeadersStatus::StopIteration;
    }

    ASSERT(!(status == FilterHeadersStatus::ContinueAndDontEndStream && !(*entry)->end_stream_),
           "Filters should not return FilterHeadersStatus::ContinueAndDontEndStream from "
           "decodeHeaders when end_stream is already false");

    state_.filter_call_state_ &= ~FilterCallState::DecodeHeaders;
    ENVOY_STREAM_LOG(trace, "decode headers called: filter={} status={}", *this,
                     (*entry)->filter_context_.config_name, static_cast<uint64_t>(status));

    (*entry)->decode_headers_called_ = true;

    const auto continue_iteration = (*entry)->commonHandleAfterHeadersCallback(status, end_stream);
    ENVOY_BUG(!continue_iteration || !state_.local_complete_,
              "Filter did not return StopAll or StopIteration after sending a local reply.");

    // If this filter ended the stream, decodeComplete() should be called for it.
    if ((*entry)->end_stream_) {
      (*entry)->handle_->decodeComplete();
    }

    // Skip processing metadata after sending local reply
    if (state_.local_complete_ && std::next(entry) != decoder_filters_.end()) {
      maybeContinueDecoding(continue_data_entry);
      return;
    }

    const bool new_metadata_added = processNewlyAddedMetadata();
    // If end_stream is set in headers, and a filter adds new metadata, we need to delay end_stream
    // in headers by inserting an empty data frame with end_stream set. The empty data frame is sent
    // after the new metadata.
    if ((*entry)->end_stream_ && new_metadata_added && !buffered_request_data_) {
      Buffer::OwnedImpl empty_data("");
      ENVOY_STREAM_LOG(
          trace, "inserting an empty data frame for end_stream due metadata being added.", *this);
      // Metadata frame doesn't carry end of stream bit. We need an empty data frame to end the
      // stream.
      addDecodedData(*((*entry).get()), empty_data, true);
    }

    if (!continue_iteration && std::next(entry) != decoder_filters_.end()) {
      // Stop iteration IFF this is not the last filter. If it is the last filter, continue with
      // processing since we need to handle the case where a terminal filter wants to buffer, but
      // a previous filter has added body.
      maybeContinueDecoding(continue_data_entry);
      return;
    }

    // Here we handle the case where we have a header only request, but a filter adds a body
    // to it. We need to not raise end_stream = true to further filters during inline iteration.
    if (end_stream && buffered_request_data_ && continue_data_entry == decoder_filters_.end()) {
      continue_data_entry = entry;
    }
  }

  maybeContinueDecoding(continue_data_entry);

  if (end_stream) {
    disarmRequestTimeout();
  }
}

void FilterManager::decodeData(ActiveStreamDecoderFilter* filter, Buffer::Instance& data,
                               bool end_stream,
                               FilterIterationStartState filter_iteration_start_state) {
  ScopeTrackerScopeState scope(&*this, dispatcher_);
  filter_manager_callbacks_.resetIdleTimer();

  // If a response is complete or a reset has been sent, filters do not care about further body
  // data. Just drop it.
  if (state_.local_complete_) {
    return;
  }

  auto trailers_added_entry = decoder_filters_.end();
  const bool trailers_exists_at_start = filter_manager_callbacks_.requestTrailers().has_value();
  // Filter iteration may start at the current filter.
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry =
      commonDecodePrefix(filter, filter_iteration_start_state);

  for (; entry != decoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame types, return now.
    if (handleDataIfStopAll(**entry, data, state_.decoder_filters_streaming_)) {
      return;
    }
    // If end_stream_ is marked for a filter, the data is not for this filter and filters after.
    //
    // In following case, ActiveStreamFilterBase::commonContinue() could be called recursively and
    // its doData() is called with wrong data.
    //
    //  There are 3 decode filters and "wrapper" refers to ActiveStreamFilter object.
    //
    //  filter0->decodeHeaders(_, true)
    //    return STOP
    //  filter0->continueDecoding()
    //    wrapper0->commonContinue()
    //      wrapper0->decodeHeaders(_, _, true)
    //        filter1->decodeHeaders(_, true)
    //          filter1->addDecodeData()
    //          return CONTINUE
    //        filter2->decodeHeaders(_, false)
    //          return CONTINUE
    //        wrapper1->commonContinue() // Detects data is added.
    //          wrapper1->doData()
    //            wrapper1->decodeData()
    //              filter2->decodeData(_, true)
    //                 return CONTINUE
    //      wrapper0->doData() // This should not be called
    //        wrapper0->decodeData()
    //          filter1->decodeData(_, true)  // It will cause assertions.
    //
    // One way to solve this problem is to mark end_stream_ for each filter.
    // If a filter is already marked as end_stream_ when decodeData() is called, bails out the
    // whole function. If just skip the filter, the codes after the loop will be called with
    // wrong data. For encodeData, the response_encoder->encode() will be called.
    if ((*entry)->end_stream_) {
      return;
    }
    ASSERT(!(state_.filter_call_state_ & FilterCallState::DecodeData));

    // We check the request_trailers_ pointer here in case addDecodedTrailers
    // is called in decodeData during a previous filter invocation, at which point we communicate to
    // the current and future filters that the stream has not yet ended.
    if (end_stream) {
      state_.filter_call_state_ |= FilterCallState::LastDataFrame;
    }

    recordLatestDataFilter(entry, state_.latest_data_decoding_filter_, decoder_filters_);

    state_.filter_call_state_ |= FilterCallState::DecodeData;
    (*entry)->end_stream_ = end_stream && !filter_manager_callbacks_.requestTrailers();
    FilterDataStatus status = (*entry)->handle_->decodeData(data, (*entry)->end_stream_);
    if ((*entry)->end_stream_) {
      (*entry)->handle_->decodeComplete();
    }
    state_.filter_call_state_ &= ~FilterCallState::DecodeData;
    if (end_stream) {
      state_.filter_call_state_ &= ~FilterCallState::LastDataFrame;
    }
    ENVOY_STREAM_LOG(trace, "decode data called: filter={} status={}", *this,
                     (*entry)->filter_context_.config_name, static_cast<uint64_t>(status));
    if (state_.decoder_filter_chain_aborted_) {
      ENVOY_STREAM_LOG(trace, "decodeData filter iteration aborted due to local reply: filter={}",
                       *this, (*entry)->filter_context_.config_name);
      return;
    }

    processNewlyAddedMetadata();

    if (!trailers_exists_at_start && filter_manager_callbacks_.requestTrailers() &&
        trailers_added_entry == decoder_filters_.end()) {
      end_stream = false;
      trailers_added_entry = entry;
    }

    if (!(*entry)->commonHandleAfterDataCallback(status, data, state_.decoder_filters_streaming_) &&
        std::next(entry) != decoder_filters_.end()) {
      // Stop iteration IFF this is not the last filter. If it is the last filter, continue with
      // processing since we need to handle the case where a terminal filter wants to buffer, but
      // a previous filter has added trailers.
      break;
    }
  }

  // If trailers were adding during decodeData we need to trigger decodeTrailers in order
  // to allow filters to process the trailers.
  if (trailers_added_entry != decoder_filters_.end()) {
    decodeTrailers(trailers_added_entry->get(), *filter_manager_callbacks_.requestTrailers());
  }

  if (end_stream) {
    disarmRequestTimeout();
  }
}

RequestTrailerMap& FilterManager::addDecodedTrailers() {
  // Trailers can only be added during the last data frame (i.e. end_stream = true).
  ASSERT(state_.filter_call_state_ & FilterCallState::LastDataFrame);

  filter_manager_callbacks_.setRequestTrailers(RequestTrailerMapImpl::create());
  return *filter_manager_callbacks_.requestTrailers();
}

void FilterManager::addDecodedData(ActiveStreamDecoderFilter& filter, Buffer::Instance& data,
                                   bool streaming) {
  if (state_.filter_call_state_ == 0 ||
      (state_.filter_call_state_ & FilterCallState::DecodeHeaders) ||
      (state_.filter_call_state_ & FilterCallState::DecodeData) ||
      ((state_.filter_call_state_ & FilterCallState::DecodeTrailers) && !filter.canIterate())) {
    // Make sure if this triggers watermarks, the correct action is taken.
    state_.decoder_filters_streaming_ = streaming;
    // If no call is happening or we are in the decode headers/data callback, buffer the data.
    // Inline processing happens in the decodeHeaders() callback if necessary.
    filter.commonHandleBufferData(data);
  } else if (state_.filter_call_state_ & FilterCallState::DecodeTrailers) {
    // In this case we need to inline dispatch the data to further filters. If those filters
    // choose to buffer/stop iteration that's fine.
    decodeData(&filter, data, false, FilterIterationStartState::AlwaysStartFromNext);
  } else {
    IS_ENVOY_BUG("Invalid request data");
    sendLocalReply(Http::Code::BadGateway, "Filter error", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().FilterAddedInvalidRequestData);
  }
}

MetadataMapVector& FilterManager::addDecodedMetadata() { return *getRequestMetadataMapVector(); }

void FilterManager::decodeTrailers(ActiveStreamDecoderFilter* filter, RequestTrailerMap& trailers) {
  // See decodeData() above for why we check local_complete_ here.
  if (state_.local_complete_) {
    return;
  }

  // Filter iteration may start at the current filter.
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry =
      commonDecodePrefix(filter, FilterIterationStartState::CanStartFromCurrent);

  for (; entry != decoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame type, return now.
    if ((*entry)->stoppedAll()) {
      return;
    }
    ASSERT(!(state_.filter_call_state_ & FilterCallState::DecodeTrailers));
    state_.filter_call_state_ |= FilterCallState::DecodeTrailers;
    FilterTrailersStatus status = (*entry)->handle_->decodeTrailers(trailers);
    (*entry)->handle_->decodeComplete();
    (*entry)->end_stream_ = true;
    state_.filter_call_state_ &= ~FilterCallState::DecodeTrailers;
    ENVOY_STREAM_LOG(trace, "decode trailers called: filter={} status={}", *this,
                     (*entry)->filter_context_.config_name, static_cast<uint64_t>(status));
    if (state_.decoder_filter_chain_aborted_) {
      ENVOY_STREAM_LOG(trace,
                       "decodeTrailers filter iteration aborted due to local reply: filter={}",
                       *this, (*entry)->filter_context_.config_name);
      status = FilterTrailersStatus::StopIteration;
    }

    processNewlyAddedMetadata();

    if (!(*entry)->commonHandleAfterTrailersCallback(status)) {
      return;
    }
  }
  disarmRequestTimeout();
}

void FilterManager::decodeMetadata(ActiveStreamDecoderFilter* filter, MetadataMap& metadata_map) {
  // Filter iteration may start at the current filter.
  std::list<ActiveStreamDecoderFilterPtr>::iterator entry =
      commonDecodePrefix(filter, FilterIterationStartState::CanStartFromCurrent);

  for (; entry != decoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame type, stores metadata and returns.
    // If the filter pointed by entry hasn't returned from decodeHeaders, stores newly added
    // metadata in case decodeHeaders returns StopAllIteration. The latter can happen when headers
    // callbacks generate new metadata.
    if (!(*entry)->decode_headers_called_ || (*entry)->stoppedAll()) {
      Http::MetadataMapPtr metadata_map_ptr = std::make_unique<Http::MetadataMap>(metadata_map);
      (*entry)->getSavedRequestMetadata()->emplace_back(std::move(metadata_map_ptr));
      return;
    }

    FilterMetadataStatus status = (*entry)->handle_->decodeMetadata(metadata_map);
    ENVOY_STREAM_LOG(trace, "decode metadata called: filter={} status={}, metadata: {}", *this,
                     (*entry)->filter_context_.config_name, static_cast<uint64_t>(status),
                     metadata_map);
  }
}

void FilterManager::disarmRequestTimeout() { filter_manager_callbacks_.disarmRequestTimeout(); }

std::list<ActiveStreamEncoderFilterPtr>::iterator
FilterManager::commonEncodePrefix(ActiveStreamEncoderFilter* filter, bool end_stream,
                                  FilterIterationStartState filter_iteration_start_state) {
  // Only do base state setting on the initial call. Subsequent calls for filtering do not touch
  // the base state.
  if (filter == nullptr) {
    ASSERT(!state_.local_complete_);
    state_.local_complete_ = end_stream;
    return encoder_filters_.begin();
  }

  if (filter_iteration_start_state == FilterIterationStartState::CanStartFromCurrent &&
      (*(filter->entry()))->iterate_from_current_filter_) {
    // The filter iteration has been stopped for all frame types, and now the iteration continues.
    // The current filter's encoding callback has not be called. Call it now.
    return filter->entry();
  }
  return std::next(filter->entry());
}

std::list<ActiveStreamDecoderFilterPtr>::iterator
FilterManager::commonDecodePrefix(ActiveStreamDecoderFilter* filter,
                                  FilterIterationStartState filter_iteration_start_state) {
  if (!filter) {
    return decoder_filters_.begin();
  }
  if (filter_iteration_start_state == FilterIterationStartState::CanStartFromCurrent &&
      (*(filter->entry()))->iterate_from_current_filter_) {
    // The filter iteration has been stopped for all frame types, and now the iteration continues.
    // The current filter's callback function has not been called. Call it now.
    return filter->entry();
  }
  return std::next(filter->entry());
}

void DownstreamFilterManager::onLocalReply(StreamFilterBase::LocalReplyData& data) {
  state_.under_on_local_reply_ = true;
  filter_manager_callbacks_.onLocalReply(data.code_);

  for (auto entry : filters_) {
    if (entry->onLocalReply(data) == LocalErrorStatus::ContinueAndResetStream) {
      data.reset_imminent_ = true;
    }
  }
  state_.under_on_local_reply_ = false;
}

void DownstreamFilterManager::sendLocalReply(
    Code code, absl::string_view body,
    const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  ASSERT(!state_.under_on_local_reply_);
  const bool is_head_request = state_.is_head_request_;
  const bool is_grpc_request = state_.is_grpc_request_;

  // Stop filter chain iteration if local reply was sent while filter decoding or encoding callbacks
  // are running.
  if (state_.filter_call_state_ & (FilterCallState::DecodeHeaders | FilterCallState::DecodeData |
                                   FilterCallState::DecodeTrailers)) {
    state_.decoder_filter_chain_aborted_ = true;
  } else if (state_.filter_call_state_ &
             (FilterCallState::EncodeHeaders | FilterCallState::EncodeData |
              FilterCallState::EncodeTrailers)) {
    state_.encoder_filter_chain_aborted_ = true;
  }

  streamInfo().setResponseCodeDetails(details);
  StreamFilterBase::LocalReplyData data{code, grpc_status, details, false};
  onLocalReply(data);
  if (data.reset_imminent_) {
    ENVOY_STREAM_LOG(debug, "Resetting stream due to {}. onLocalReply requested reset.", *this,
                     details);
    filter_manager_callbacks_.resetStream(Http::StreamResetReason::LocalReset, "");
    return;
  }

  if (!filter_manager_callbacks_.responseHeaders().has_value()) {
    // If the response has not started at all, send the response through the filter chain.
    sendLocalReplyViaFilterChain(is_grpc_request, code, body, modify_headers, is_head_request,
                                 grpc_status, details);
  } else if (!state_.non_100_response_headers_encoded_) {
    ENVOY_STREAM_LOG(debug, "Sending local reply with details {} directly to the encoder", *this,
                     details);
    // In this case, at least the header and possibly the body has started
    // processing through the filter chain, but no non-informational headers
    // have been sent downstream. To ensure that filters don't get their
    // state machine screwed up, bypass the filter chain and send the local
    // reply directly to the codec.
    //
    sendDirectLocalReply(code, body, modify_headers, state_.is_head_request_, grpc_status);
  } else {
    // If we land in this branch, response headers have already been sent to the client.
    // All we can do at this point is reset the stream.
    ENVOY_STREAM_LOG(debug, "Resetting stream due to {}. Prior headers have already been sent",
                     *this, details);
    // TODO(snowp): This means we increment the tx_reset stat which we weren't doing previously.
    // Intended?
    filter_manager_callbacks_.resetStream();
  }
}

void DownstreamFilterManager::sendLocalReplyViaFilterChain(
    bool is_grpc_request, Code code, absl::string_view body,
    const std::function<void(ResponseHeaderMap& headers)>& modify_headers, bool is_head_request,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  ENVOY_STREAM_LOG(debug, "Sending local reply with details {}", *this, details);
  ASSERT(!filter_manager_callbacks_.responseHeaders().has_value());
  // For early error handling, do a best-effort attempt to create a filter chain
  // to ensure access logging. If the filter chain already exists this will be
  // a no-op.
  createFilterChain();

  Utility::sendLocalReply(
      state_.destroyed_,
      Utility::EncodeFunctions{
          [this, modify_headers](ResponseHeaderMap& headers) -> void {
            if (streamInfo().route() && streamInfo().route()->routeEntry()) {
              streamInfo().route()->routeEntry()->finalizeResponseHeaders(headers, streamInfo());
            }
            if (modify_headers) {
              modify_headers(headers);
            }
          },
          [this](ResponseHeaderMap& response_headers, Code& code, std::string& body,
                 absl::string_view& content_type) -> void {
            local_reply_.rewrite(filter_manager_callbacks_.requestHeaders().ptr(), response_headers,
                                 streamInfo(), code, body, content_type);
          },
          [this, modify_headers](ResponseHeaderMapPtr&& headers, bool end_stream) -> void {
            filter_manager_callbacks_.setResponseHeaders(std::move(headers));
            encodeHeaders(nullptr, filter_manager_callbacks_.responseHeaders().ref(), end_stream);
          },
          [this](Buffer::Instance& data, bool end_stream) -> void {
            encodeData(nullptr, data, end_stream,
                       FilterManager::FilterIterationStartState::CanStartFromCurrent);
          }},
      Utility::LocalReplyData{is_grpc_request, code, body, grpc_status, is_head_request});
}

void DownstreamFilterManager::sendDirectLocalReply(
    Code code, absl::string_view body,
    const std::function<void(ResponseHeaderMap&)>& modify_headers, bool is_head_request,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status) {
  // Make sure we won't end up with nested watermark calls from the body buffer.
  state_.encoder_filters_streaming_ = true;
  Http::Utility::sendLocalReply(
      state_.destroyed_,
      Utility::EncodeFunctions{
          [this, modify_headers](ResponseHeaderMap& headers) -> void {
            if (streamInfo().route() && streamInfo().route()->routeEntry()) {
              streamInfo().route()->routeEntry()->finalizeResponseHeaders(headers, streamInfo());
            }
            if (modify_headers) {
              modify_headers(headers);
            }
          },
          [&](ResponseHeaderMap& response_headers, Code& code, std::string& body,
              absl::string_view& content_type) -> void {
            local_reply_.rewrite(filter_manager_callbacks_.requestHeaders().ptr(), response_headers,
                                 streamInfo(), code, body, content_type);
          },
          [&](ResponseHeaderMapPtr&& response_headers, bool end_stream) -> void {
            // Move the response headers into the FilterManager to make sure they're visible to
            // access logs.
            filter_manager_callbacks_.setResponseHeaders(std::move(response_headers));

            state_.non_100_response_headers_encoded_ = true;
            filter_manager_callbacks_.encodeHeaders(*filter_manager_callbacks_.responseHeaders(),
                                                    end_stream);
            if (state_.saw_downstream_reset_) {
              return;
            }
            maybeEndEncode(end_stream);
          },
          [&](Buffer::Instance& data, bool end_stream) -> void {
            filter_manager_callbacks_.encodeData(data, end_stream);
            if (state_.saw_downstream_reset_) {
              return;
            }
            maybeEndEncode(end_stream);
          }},
      Utility::LocalReplyData{state_.is_grpc_request_, code, body, grpc_status, is_head_request});
}

void FilterManager::encode1xxHeaders(ActiveStreamEncoderFilter* filter,
                                     ResponseHeaderMap& headers) {
  filter_manager_callbacks_.resetIdleTimer();
  ASSERT(proxy_100_continue_);
  // The caller must guarantee that encode1xxHeaders() is invoked at most once.
  ASSERT(!state_.has_1xx_headers_ || filter != nullptr);
  // Make sure commonContinue continues encode1xxHeaders.
  state_.has_1xx_headers_ = true;

  // Similar to the block in encodeHeaders, run encode1xxHeaders on each
  // filter. This is simpler than that case because 100 continue implies no
  // end-stream, and because there are normal headers coming there's no need for
  // complex continuation logic.
  // 100-continue filter iteration should always start with the next filter if available.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, false, FilterIterationStartState::AlwaysStartFromNext);
  for (; entry != encoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::Encode1xxHeaders));
    state_.filter_call_state_ |= FilterCallState::Encode1xxHeaders;
    FilterHeadersStatus status = (*entry)->handle_->encode1xxHeaders(headers);
    state_.filter_call_state_ &= ~FilterCallState::Encode1xxHeaders;
    ENVOY_STREAM_LOG(trace, "encode 1xx continue headers called: filter={} status={}", *this,
                     (*entry)->filter_context_.config_name, static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfter1xxHeadersCallback(status)) {
      return;
    }
  }

  filter_manager_callbacks_.encode1xxHeaders(headers);
}

void FilterManager::maybeContinueEncoding(
    const std::list<ActiveStreamEncoderFilterPtr>::iterator& continue_data_entry) {
  if (continue_data_entry != encoder_filters_.end()) {
    // We use the continueEncoding() code since it will correctly handle not calling
    // encodeHeaders() again. Fake setting StopSingleIteration since the continueEncoding() code
    // expects it.
    ASSERT(buffered_response_data_);
    (*continue_data_entry)->iteration_state_ =
        ActiveStreamFilterBase::IterationState::StopSingleIteration;
    (*continue_data_entry)->continueEncoding();
  }
}

void FilterManager::encodeHeaders(ActiveStreamEncoderFilter* filter, ResponseHeaderMap& headers,
                                  bool end_stream) {
  // See encodeHeaders() comments in include/envoy/http/filter.h for why the 1xx precondition holds.
  ASSERT(!CodeUtility::is1xx(Utility::getResponseStatus(headers)) ||
         Utility::getResponseStatus(headers) == enumToInt(Http::Code::SwitchingProtocols));
  filter_manager_callbacks_.resetIdleTimer();
  disarmRequestTimeout();

  // Headers filter iteration should always start with the next filter if available.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, end_stream, FilterIterationStartState::AlwaysStartFromNext);
  std::list<ActiveStreamEncoderFilterPtr>::iterator continue_data_entry = encoder_filters_.end();

  for (; entry != encoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::EncodeHeaders));
    state_.filter_call_state_ |= FilterCallState::EncodeHeaders;
    (*entry)->end_stream_ = (end_stream && continue_data_entry == encoder_filters_.end());
    FilterHeadersStatus status = (*entry)->handle_->encodeHeaders(headers, (*entry)->end_stream_);
    if (state_.encoder_filter_chain_aborted_) {
      ENVOY_STREAM_LOG(trace,
                       "encodeHeaders filter iteration aborted due to local reply: filter={}",
                       *this, (*entry)->filter_context_.config_name);
      status = FilterHeadersStatus::StopIteration;
    }

    ASSERT(!(status == FilterHeadersStatus::ContinueAndDontEndStream && !(*entry)->end_stream_),
           "Filters should not return FilterHeadersStatus::ContinueAndDontEndStream from "
           "encodeHeaders when end_stream is already false");

    state_.filter_call_state_ &= ~FilterCallState::EncodeHeaders;
    ENVOY_STREAM_LOG(trace, "encode headers called: filter={} status={}", *this,
                     (*entry)->filter_context_.config_name, static_cast<uint64_t>(status));

    (*entry)->encode_headers_called_ = true;

    const auto continue_iteration = (*entry)->commonHandleAfterHeadersCallback(status, end_stream);

    // If this filter ended the stream, encodeComplete() should be called for it.
    if ((*entry)->end_stream_) {
      (*entry)->handle_->encodeComplete();
    }

    if (!continue_iteration) {
      if (!(*entry)->end_stream_) {
        maybeContinueEncoding(continue_data_entry);
      }
      return;
    }

    // Here we handle the case where we have a header only response, but a filter adds a body
    // to it. We need to not raise end_stream = true to further filters during inline iteration.
    if (end_stream && buffered_response_data_ && continue_data_entry == encoder_filters_.end()) {
      continue_data_entry = entry;
    }
  }

  // Check if the filter chain above did not remove critical headers or set malformed header values.
  // We could do this at the codec in order to prevent other places than the filter chain from
  // removing critical headers, but it will come with the implementation complexity.
  // See the previous attempt (#15658) for detail, and for now we choose to protect only against
  // filter chains.
  const auto status = HeaderUtility::checkRequiredResponseHeaders(headers);
  if (!status.ok()) {
    // If the check failed, then we reply with BadGateway, and stop the further processing.
    sendLocalReply(
        Http::Code::BadGateway, status.message(), nullptr, absl::nullopt,
        absl::StrCat(StreamInfo::ResponseCodeDetails::get().FilterRemovedRequiredResponseHeaders,
                     "{", StringUtil::replaceAllEmptySpace(status.message()), "}"));
    return;
  }

  const bool modified_end_stream = (end_stream && continue_data_entry == encoder_filters_.end());
  state_.non_100_response_headers_encoded_ = true;
  filter_manager_callbacks_.encodeHeaders(headers, modified_end_stream);
  if (state_.saw_downstream_reset_) {
    return;
  }
  maybeEndEncode(modified_end_stream);

  if (!modified_end_stream) {
    maybeContinueEncoding(continue_data_entry);
  }
}

void FilterManager::encodeMetadata(ActiveStreamEncoderFilter* filter,
                                   MetadataMapPtr&& metadata_map_ptr) {
  filter_manager_callbacks_.resetIdleTimer();

  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, false, FilterIterationStartState::CanStartFromCurrent);

  for (; entry != encoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame type, stores metadata and returns.
    // If the filter pointed by entry hasn't returned from encodeHeaders, stores newly added
    // metadata in case encodeHeaders returns StopAllIteration. The latter can happen when headers
    // callbacks generate new metadata.
    if (!(*entry)->encode_headers_called_ || (*entry)->stoppedAll()) {
      (*entry)->getSavedResponseMetadata()->emplace_back(std::move(metadata_map_ptr));
      return;
    }

    FilterMetadataStatus status = (*entry)->handle_->encodeMetadata(*metadata_map_ptr);
    ENVOY_STREAM_LOG(trace, "encode metadata called: filter={} status={}", *this,
                     (*entry)->filter_context_.config_name, static_cast<uint64_t>(status));
  }
  // TODO(soya3129): update stats with metadata.

  // Now encode metadata via the codec.
  if (!metadata_map_ptr->empty()) {
    filter_manager_callbacks_.encodeMetadata(std::move(metadata_map_ptr));
  }
}

ResponseTrailerMap& FilterManager::addEncodedTrailers() {
  // Trailers can only be added during the last data frame (i.e. end_stream = true).
  ASSERT(state_.filter_call_state_ & FilterCallState::LastDataFrame);

  // Trailers can only be added once.
  ASSERT(!filter_manager_callbacks_.responseTrailers());

  filter_manager_callbacks_.setResponseTrailers(ResponseTrailerMapImpl::create());
  return *filter_manager_callbacks_.responseTrailers();
}

void FilterManager::addEncodedData(ActiveStreamEncoderFilter& filter, Buffer::Instance& data,
                                   bool streaming) {
  if (state_.filter_call_state_ == 0 ||
      (state_.filter_call_state_ & FilterCallState::EncodeHeaders) ||
      (state_.filter_call_state_ & FilterCallState::EncodeData) ||
      ((state_.filter_call_state_ & FilterCallState::EncodeTrailers) && !filter.canIterate())) {
    // Make sure if this triggers watermarks, the correct action is taken.
    state_.encoder_filters_streaming_ = streaming;
    // If no call is happening or we are in the decode headers/data callback, buffer the data.
    // Inline processing happens in the decodeHeaders() callback if necessary.
    filter.commonHandleBufferData(data);
  } else if (state_.filter_call_state_ & FilterCallState::EncodeTrailers) {
    // In this case we need to inline dispatch the data to further filters. If those filters
    // choose to buffer/stop iteration that's fine.
    encodeData(&filter, data, false, FilterIterationStartState::AlwaysStartFromNext);
  } else {
    IS_ENVOY_BUG("Invalid response data");
    sendLocalReply(Http::Code::BadGateway, "Filter error", nullptr, absl::nullopt,
                   StreamInfo::ResponseCodeDetails::get().FilterAddedInvalidResponseData);
  }
}

void FilterManager::encodeData(ActiveStreamEncoderFilter* filter, Buffer::Instance& data,
                               bool end_stream,
                               FilterIterationStartState filter_iteration_start_state) {
  filter_manager_callbacks_.resetIdleTimer();

  // Filter iteration may start at the current filter.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, end_stream, filter_iteration_start_state);
  auto trailers_added_entry = encoder_filters_.end();

  const bool trailers_exists_at_start = filter_manager_callbacks_.responseTrailers().has_value();
  for (; entry != encoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame type, return now.
    if (handleDataIfStopAll(**entry, data, state_.encoder_filters_streaming_)) {
      return;
    }
    // If end_stream_ is marked for a filter, the data is not for this filter and filters after.
    // For details, please see the comment in the ActiveStream::decodeData() function.
    if ((*entry)->end_stream_) {
      return;
    }
    ASSERT(!(state_.filter_call_state_ & FilterCallState::EncodeData));

    // We check the response_trailers_ pointer here in case addEncodedTrailers
    // is called in encodeData during a previous filter invocation, at which point we communicate to
    // the current and future filters that the stream has not yet ended.
    state_.filter_call_state_ |= FilterCallState::EncodeData;
    if (end_stream) {
      state_.filter_call_state_ |= FilterCallState::LastDataFrame;
    }

    recordLatestDataFilter(entry, state_.latest_data_encoding_filter_, encoder_filters_);

    (*entry)->end_stream_ = end_stream && !filter_manager_callbacks_.responseTrailers();
    FilterDataStatus status = (*entry)->handle_->encodeData(data, (*entry)->end_stream_);
    if (state_.encoder_filter_chain_aborted_) {
      ENVOY_STREAM_LOG(trace, "encodeData filter iteration aborted due to local reply: filter={}",
                       *this, (*entry)->filter_context_.config_name);
      status = FilterDataStatus::StopIterationNoBuffer;
    }
    if ((*entry)->end_stream_) {
      (*entry)->handle_->encodeComplete();
    }
    state_.filter_call_state_ &= ~FilterCallState::EncodeData;
    if (end_stream) {
      state_.filter_call_state_ &= ~FilterCallState::LastDataFrame;
    }
    ENVOY_STREAM_LOG(trace, "encode data called: filter={} status={}", *this,
                     (*entry)->filter_context_.config_name, static_cast<uint64_t>(status));

    if (!trailers_exists_at_start && filter_manager_callbacks_.responseTrailers() &&
        trailers_added_entry == encoder_filters_.end()) {
      trailers_added_entry = entry;
    }

    if (!(*entry)->commonHandleAfterDataCallback(status, data, state_.encoder_filters_streaming_)) {
      return;
    }
  }

  const bool modified_end_stream = end_stream && trailers_added_entry == encoder_filters_.end();
  filter_manager_callbacks_.encodeData(data, modified_end_stream);
  if (state_.saw_downstream_reset_) {
    return;
  }
  maybeEndEncode(modified_end_stream);

  // If trailers were adding during encodeData we need to trigger decodeTrailers in order
  // to allow filters to process the trailers.
  if (trailers_added_entry != encoder_filters_.end()) {
    encodeTrailers(trailers_added_entry->get(), *filter_manager_callbacks_.responseTrailers());
  }
}

void FilterManager::encodeTrailers(ActiveStreamEncoderFilter* filter,
                                   ResponseTrailerMap& trailers) {
  filter_manager_callbacks_.resetIdleTimer();

  // Filter iteration may start at the current filter.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, true, FilterIterationStartState::CanStartFromCurrent);
  for (; entry != encoder_filters_.end(); entry++) {
    // If the filter pointed by entry has stopped for all frame type, return now.
    if ((*entry)->stoppedAll()) {
      return;
    }
    ASSERT(!(state_.filter_call_state_ & FilterCallState::EncodeTrailers));
    state_.filter_call_state_ |= FilterCallState::EncodeTrailers;
    FilterTrailersStatus status = (*entry)->handle_->encodeTrailers(trailers);
    (*entry)->handle_->encodeComplete();
    (*entry)->end_stream_ = true;
    state_.filter_call_state_ &= ~FilterCallState::EncodeTrailers;
    ENVOY_STREAM_LOG(trace, "encode trailers called: filter={} status={}", *this,
                     (*entry)->filter_context_.config_name, static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterTrailersCallback(status)) {
      return;
    }
  }

  filter_manager_callbacks_.encodeTrailers(trailers);
  if (state_.saw_downstream_reset_) {
    return;
  }
  maybeEndEncode(true);
}

void FilterManager::maybeEndEncode(bool end_stream) {
  if (end_stream) {
    ASSERT(!state_.remote_encode_complete_);
    state_.remote_encode_complete_ = true;
    filter_manager_callbacks_.endStream();
  }
}

bool FilterManager::processNewlyAddedMetadata() {
  if (request_metadata_map_vector_ == nullptr) {
    return false;
  }
  for (const auto& metadata_map : *getRequestMetadataMapVector()) {
    decodeMetadata(nullptr, *metadata_map);
  }
  getRequestMetadataMapVector()->clear();
  return true;
}

bool FilterManager::handleDataIfStopAll(ActiveStreamFilterBase& filter, Buffer::Instance& data,
                                        bool& filter_streaming) {
  if (filter.stoppedAll()) {
    ASSERT(!filter.canIterate());
    filter_streaming =
        filter.iteration_state_ == ActiveStreamFilterBase::IterationState::StopAllWatermark;
    filter.commonHandleBufferData(data);
    return true;
  }
  return false;
}

void FilterManager::callHighWatermarkCallbacks() {
  ++high_watermark_count_;
  for (auto watermark_callbacks : watermark_callbacks_) {
    watermark_callbacks->onAboveWriteBufferHighWatermark();
  }
}

void FilterManager::callLowWatermarkCallbacks() {
  ASSERT(high_watermark_count_ > 0);
  --high_watermark_count_;
  for (auto watermark_callbacks : watermark_callbacks_) {
    watermark_callbacks->onBelowWriteBufferLowWatermark();
  }
}

void FilterManager::setBufferLimit(uint32_t new_limit) {
  ENVOY_STREAM_LOG(debug, "setting buffer limit to {}", *this, new_limit);
  buffer_limit_ = new_limit;
  if (buffered_request_data_) {
    buffered_request_data_->setWatermarks(buffer_limit_);
  }
  if (buffered_response_data_) {
    buffered_response_data_->setWatermarks(buffer_limit_);
  }
}

void FilterManager::contextOnContinue(ScopeTrackedObjectStack& tracked_object_stack) {
  if (connection_.has_value()) {
    tracked_object_stack.add(*connection_);
  }
  tracked_object_stack.add(filter_manager_callbacks_.scope());
}

bool FilterManager::createFilterChain() {
  if (state_.created_filter_chain_) {
    return false;
  }
  bool upgrade_rejected = false;
  const HeaderEntry* upgrade = nullptr;
  if (filter_manager_callbacks_.requestHeaders()) {
    upgrade = filter_manager_callbacks_.requestHeaders()->Upgrade();

    // Treat CONNECT requests as a special upgrade case.
    if (!upgrade && HeaderUtility::isConnect(*filter_manager_callbacks_.requestHeaders())) {
      upgrade = filter_manager_callbacks_.requestHeaders()->Method();
    }
  }

  state_.created_filter_chain_ = true;
  if (upgrade != nullptr) {
    const Router::RouteEntry::UpgradeMap* upgrade_map = filter_manager_callbacks_.upgradeMap();

    if (filter_chain_factory_.createUpgradeFilterChain(upgrade->value().getStringView(),
                                                       upgrade_map, *this)) {
      filter_manager_callbacks_.upgradeFilterChainCreated();
      return true;
    } else {
      upgrade_rejected = true;
      // Fall through to the default filter chain. The function calling this
      // will send a local reply indicating that the upgrade failed.
    }
  }

  filter_chain_factory_.createFilterChain(*this);
  return !upgrade_rejected;
}

void ActiveStreamDecoderFilter::requestDataDrained() {
  // If this is called it means the call to requestDataTooLarge() was a
  // streaming call, or a 413 would have been sent.
  onDecoderFilterBelowWriteBufferLowWatermark();
}

void ActiveStreamDecoderFilter::onDecoderFilterBelowWriteBufferLowWatermark() {
  parent_.filter_manager_callbacks_.onDecoderFilterBelowWriteBufferLowWatermark();
}

void ActiveStreamDecoderFilter::addDownstreamWatermarkCallbacks(
    DownstreamWatermarkCallbacks& watermark_callbacks) {
  // This is called exactly once per upstream-stream, by the router filter. Therefore, we
  // expect the same callbacks to not be registered twice.
  ASSERT(std::find(parent_.watermark_callbacks_.begin(), parent_.watermark_callbacks_.end(),
                   &watermark_callbacks) == parent_.watermark_callbacks_.end());
  parent_.watermark_callbacks_.emplace(parent_.watermark_callbacks_.end(), &watermark_callbacks);
  for (uint32_t i = 0; i < parent_.high_watermark_count_; ++i) {
    watermark_callbacks.onAboveWriteBufferHighWatermark();
  }
}

void ActiveStreamDecoderFilter::removeDownstreamWatermarkCallbacks(
    DownstreamWatermarkCallbacks& watermark_callbacks) {
  ASSERT(std::find(parent_.watermark_callbacks_.begin(), parent_.watermark_callbacks_.end(),
                   &watermark_callbacks) != parent_.watermark_callbacks_.end());
  parent_.watermark_callbacks_.remove(&watermark_callbacks);
}

void ActiveStreamDecoderFilter::setDecoderBufferLimit(uint32_t limit) {
  parent_.setBufferLimit(limit);
}

uint32_t ActiveStreamDecoderFilter::decoderBufferLimit() { return parent_.buffer_limit_; }

bool ActiveStreamDecoderFilter::recreateStream(const ResponseHeaderMap* headers) {
  // Because the filter's and the HCM view of if the stream has a body and if
  // the stream is complete may differ, re-check bytesReceived() to make sure
  // there was no body from the HCM's point of view.
  if (!complete()) {
    return false;
  }

  parent_.streamInfo().setResponseCodeDetails(
      StreamInfo::ResponseCodeDetails::get().InternalRedirect);

  if (headers != nullptr) {
    // The call to setResponseHeaders is needed to ensure that the headers are properly logged in
    // access logs before the stream is destroyed. Since the function expects a ResponseHeaderPtr&&,
    // ownership of the headers must be passed. This cannot happen earlier in the flow (such as in
    // the call to setupRedirect) because at that point it is still possible for the headers to be
    // used in a different logical branch. We work around this by creating a copy and passing
    // ownership of the copy instead.
    ResponseHeaderMapPtr headers_copy = createHeaderMap<ResponseHeaderMapImpl>(*headers);
    parent_.filter_manager_callbacks_.setResponseHeaders(std::move(headers_copy));
    parent_.filter_manager_callbacks_.chargeStats(*headers);
  }

  parent_.filter_manager_callbacks_.recreateStream(parent_.streamInfo().filterState());

  return true;
}

void ActiveStreamDecoderFilter::addUpstreamSocketOptions(
    const Network::Socket::OptionsSharedPtr& options) {

  Network::Socket::appendOptions(parent_.upstream_options_, options);
}

Network::Socket::OptionsSharedPtr ActiveStreamDecoderFilter::getUpstreamSocketOptions() const {
  return parent_.upstream_options_;
}

Buffer::InstancePtr ActiveStreamEncoderFilter::createBuffer() {
  auto buffer = dispatcher().getWatermarkFactory().createBuffer(
      [this]() -> void { this->responseDataDrained(); },
      [this]() -> void { this->responseDataTooLarge(); },
      []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
  buffer->setWatermarks(parent_.buffer_limit_);
  return buffer;
}
Buffer::InstancePtr& ActiveStreamEncoderFilter::bufferedData() {
  return parent_.buffered_response_data_;
}
bool ActiveStreamEncoderFilter::complete() { return parent_.state_.local_complete_; }
bool ActiveStreamEncoderFilter::has1xxHeaders() {
  return parent_.state_.has_1xx_headers_ && !continued_1xx_headers_;
}
void ActiveStreamEncoderFilter::do1xxHeaders() {
  parent_.encode1xxHeaders(this, *parent_.filter_manager_callbacks_.informationalHeaders());
}
void ActiveStreamEncoderFilter::doHeaders(bool end_stream) {
  parent_.encodeHeaders(this, *parent_.filter_manager_callbacks_.responseHeaders(), end_stream);
}
void ActiveStreamEncoderFilter::doData(bool end_stream) {
  parent_.encodeData(this, *parent_.buffered_response_data_, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}
void ActiveStreamEncoderFilter::drainSavedResponseMetadata() {
  ASSERT(saved_response_metadata_ != nullptr);
  for (auto& metadata_map : *getSavedResponseMetadata()) {
    parent_.encodeMetadata(this, std::move(metadata_map));
  }
  getSavedResponseMetadata()->clear();
}

void ActiveStreamEncoderFilter::handleMetadataAfterHeadersCallback() {
  // If we drain accumulated metadata, the iteration must start with the current filter.
  const bool saved_state = iterate_from_current_filter_;
  iterate_from_current_filter_ = true;
  // If encodeHeaders() returns StopAllIteration, we should skip draining metadata, and wait
  // for doMetadata() to drain the metadata after iteration continues.
  if (!stoppedAll() && saved_response_metadata_ != nullptr &&
      !getSavedResponseMetadata()->empty()) {
    drainSavedResponseMetadata();
  }
  // Restores the original value of iterate_from_current_filter_.
  iterate_from_current_filter_ = saved_state;
}
void ActiveStreamEncoderFilter::doTrailers() {
  parent_.encodeTrailers(this, *parent_.filter_manager_callbacks_.responseTrailers());
}
bool ActiveStreamEncoderFilter::hasTrailers() {
  return parent_.filter_manager_callbacks_.responseTrailers().has_value();
}
void ActiveStreamEncoderFilter::addEncodedData(Buffer::Instance& data, bool streaming) {
  return parent_.addEncodedData(*this, data, streaming);
}

void ActiveStreamEncoderFilter::injectEncodedDataToFilterChain(Buffer::Instance& data,
                                                               bool end_stream) {
  if (!headers_continued_) {
    headers_continued_ = true;
    doHeaders(false);
  }
  parent_.encodeData(this, data, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}

ResponseTrailerMap& ActiveStreamEncoderFilter::addEncodedTrailers() {
  return parent_.addEncodedTrailers();
}

void ActiveStreamEncoderFilter::addEncodedMetadata(MetadataMapPtr&& metadata_map_ptr) {
  return parent_.encodeMetadata(this, std::move(metadata_map_ptr));
}

void ActiveStreamEncoderFilter::onEncoderFilterAboveWriteBufferHighWatermark() {
  ENVOY_STREAM_LOG(debug, "Disabling upstream stream due to filter callbacks.", parent_);
  parent_.callHighWatermarkCallbacks();
}

void ActiveStreamEncoderFilter::onEncoderFilterBelowWriteBufferLowWatermark() {
  ENVOY_STREAM_LOG(debug, "Enabling upstream stream due to filter callbacks.", parent_);
  parent_.callLowWatermarkCallbacks();
}

void ActiveStreamEncoderFilter::setEncoderBufferLimit(uint32_t limit) {
  parent_.setBufferLimit(limit);
}

uint32_t ActiveStreamEncoderFilter::encoderBufferLimit() { return parent_.buffer_limit_; }

void ActiveStreamEncoderFilter::continueEncoding() { commonContinue(); }

const Buffer::Instance* ActiveStreamEncoderFilter::encodingBuffer() {
  return parent_.buffered_response_data_.get();
}

void ActiveStreamEncoderFilter::modifyEncodingBuffer(
    std::function<void(Buffer::Instance&)> callback) {
  ASSERT(parent_.state_.latest_data_encoding_filter_ == this);
  callback(*parent_.buffered_response_data_.get());
}

void ActiveStreamEncoderFilter::sendLocalReply(
    Code code, absl::string_view body,
    std::function<void(ResponseHeaderMap& headers)> modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  parent_.sendLocalReply(code, body, modify_headers, grpc_status, details);
}

void ActiveStreamEncoderFilter::responseDataTooLarge() {
  ENVOY_STREAM_LOG(debug, "response data too large watermark exceeded", parent_);
  if (parent_.state_.encoder_filters_streaming_) {
    onEncoderFilterAboveWriteBufferHighWatermark();
  } else {
    parent_.filter_manager_callbacks_.onResponseDataTooLarge();

    // In this case, sendLocalReply will either send a response directly to the encoder, or
    // reset the stream.
    parent_.sendLocalReply(
        Http::Code::InternalServerError, CodeUtility::toString(Http::Code::InternalServerError),
        nullptr, absl::nullopt, StreamInfo::ResponseCodeDetails::get().ResponsePayloadTooLarge);
  }
}

void ActiveStreamEncoderFilter::responseDataDrained() {
  onEncoderFilterBelowWriteBufferLowWatermark();
}

void ActiveStreamFilterBase::resetStream(Http::StreamResetReason reset_reason,
                                         absl::string_view transport_failure_reason) {
  parent_.filter_manager_callbacks_.resetStream(reset_reason, transport_failure_reason);
}

uint64_t ActiveStreamFilterBase::streamId() const { return parent_.streamId(); }

Buffer::BufferMemoryAccountSharedPtr ActiveStreamDecoderFilter::account() const {
  return parent_.account();
}

void ActiveStreamDecoderFilter::setUpstreamOverrideHost(absl::string_view host) {
  parent_.upstream_override_host_.emplace(std::move(host));
}

absl::optional<absl::string_view> ActiveStreamDecoderFilter::upstreamOverrideHost() const {
  return parent_.upstream_override_host_;
}

} // namespace Http
} // namespace Envoy
