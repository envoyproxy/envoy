#include "common/http/filter_manager.h"

#include "common/common/enum_to_int.h"
#include "common/common/scope_tracker.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"
#include "common/http/utility.h"

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
  // TODO(mattklein123): Raise an error if this is called during a callback.
  if (!canContinue()) {
    ENVOY_STREAM_LOG(trace, "cannot continue filter chain: filter={}", *this,
                     static_cast<const void*>(this));
    return;
  }

  ENVOY_STREAM_LOG(trace, "continuing filter chain: filter={}", *this,
                   static_cast<const void*>(this));
  ASSERT(!canIterate());
  // If iteration has stopped for all frame types, set iterate_from_current_filter_ to true so the
  // filter iteration starts with the current filter instead of the next one.
  if (stoppedAll()) {
    iterate_from_current_filter_ = true;
  }
  allowIteration();

  // Only resume with do100ContinueHeaders() if we've actually seen a 100-Continue.
  if (has100Continueheaders()) {
    continue_headers_continued_ = true;
    do100ContinueHeaders();
    // If the response headers have not yet come in, don't continue on with
    // headers and body. doHeaders expects request headers to exist.
    if (!parent_.response_headers_.get()) {
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

  if (bufferedData()) {
    doData(complete() && !hasTrailers());
  }

  if (hasTrailers()) {
    doTrailers();
  }

  iterate_from_current_filter_ = false;
}

bool ActiveStreamFilterBase::commonHandleAfter100ContinueHeadersCallback(
    FilterHeadersStatus status) {
  ASSERT(parent_.state_.has_continue_headers_);
  ASSERT(!continue_headers_continued_);
  ASSERT(canIterate());

  if (status == FilterHeadersStatus::StopIteration) {
    iteration_state_ = IterationState::StopSingleIteration;
    return false;
  } else {
    ASSERT(status == FilterHeadersStatus::Continue);
    continue_headers_continued_ = true;
    return true;
  }
}

bool ActiveStreamFilterBase::commonHandleAfterHeadersCallback(FilterHeadersStatus status,
                                                              bool& headers_only) {
  ASSERT(!headers_continued_);
  ASSERT(canIterate());

  if (status == FilterHeadersStatus::StopIteration) {
    iteration_state_ = IterationState::StopSingleIteration;
  } else if (status == FilterHeadersStatus::StopAllIterationAndBuffer) {
    iteration_state_ = IterationState::StopAllBuffer;
  } else if (status == FilterHeadersStatus::StopAllIterationAndWatermark) {
    iteration_state_ = IterationState::StopAllWatermark;
  } else if (status == FilterHeadersStatus::ContinueAndEndStream) {
    // Set headers_only to true so we know to end early if necessary,
    // but continue filter iteration so we actually write the headers/run the cleanup code.
    headers_only = true;
    ENVOY_STREAM_LOG(debug, "converting to headers only", parent_);
  } else {
    ASSERT(status == FilterHeadersStatus::Continue);
    headers_continued_ = true;
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
    } else if (complete() && !hasTrailers() && !bufferedData()) {
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
  } else {
    return false;
  }

  return true;
}

const Network::Connection* ActiveStreamFilterBase::connection() { return parent_.connection(); }

Event::Dispatcher& ActiveStreamFilterBase::dispatcher() { return parent_.dispatcher_; }

StreamInfo::StreamInfo& ActiveStreamFilterBase::streamInfo() { return parent_.stream_info_; }

Tracing::Span& ActiveStreamFilterBase::activeSpan() {
  return parent_.filter_manager_callbacks_.activeSpan();
}

const ScopeTrackedObject& ActiveStreamFilterBase::scope() {
  return parent_.filter_manager_callbacks_.scope();
}

Tracing::Config& ActiveStreamFilterBase::tracingConfig() {
  return parent_.filter_manager_callbacks_.tracingConfig();
}

Upstream::ClusterInfoConstSharedPtr ActiveStreamFilterBase::clusterInfo() {
  return parent_.filter_manager_callbacks_.clusterInfo();
}

Router::RouteConstSharedPtr ActiveStreamFilterBase::route() { return route(nullptr); }

Router::RouteConstSharedPtr ActiveStreamFilterBase::route(const Router::RouteCallback& cb) {
  return parent_.filter_manager_callbacks_.route(cb);
}

void ActiveStreamFilterBase::clearRouteCache() {
  parent_.filter_manager_callbacks_.clearRouteCache();
}

bool ActiveStreamDecoderFilter::canContinue() {
  // It is possible for the connection manager to respond directly to a request even while
  // a filter is trying to continue. If a response has already happened, we should not
  // continue to further filters. A concrete example of this is a filter buffering data, the
  // last data frame comes in and the filter continues, but the final buffering takes the stream
  // over the high watermark such that a 413 is returned.
  return !parent_.state_.local_complete_;
}

Buffer::WatermarkBufferPtr ActiveStreamDecoderFilter::createBuffer() {
  auto buffer = std::make_unique<Buffer::WatermarkBuffer>(
      [this]() -> void { this->requestDataDrained(); },
      [this]() -> void { this->requestDataTooLarge(); },
      []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
  buffer->setWatermarks(parent_.buffer_limit_);
  return buffer;
}

Buffer::WatermarkBufferPtr& ActiveStreamDecoderFilter::bufferedData() {
  return parent_.buffered_request_data_;
}

bool ActiveStreamDecoderFilter::complete() { return parent_.state_.remote_complete_; }

void ActiveStreamDecoderFilter::doHeaders(bool end_stream) {
  parent_.decodeHeaders(this, *parent_.request_headers_, end_stream);
}

void ActiveStreamDecoderFilter::doData(bool end_stream) {
  parent_.decodeData(this, *parent_.buffered_request_data_, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}

void ActiveStreamDecoderFilter::doTrailers() {
  parent_.decodeTrailers(this, *parent_.request_trailers_);
}
bool ActiveStreamDecoderFilter::hasTrailers() { return parent_.request_trailers_ != nullptr; }

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
  parent_.stream_info_.setResponseCodeDetails(details);
  parent_.sendLocalReply(is_grpc_request_, code, body, modify_headers, grpc_status, details);
}

void ActiveStreamDecoderFilter::encode100ContinueHeaders(ResponseHeaderMapPtr&& headers) {
  // If Envoy is not configured to proxy 100-Continue responses, swallow the 100 Continue
  // here. This avoids the potential situation where Envoy strips Expect: 100-Continue and sends a
  // 100-Continue, then proxies a duplicate 100 Continue from upstream.
  if (parent_.proxy_100_continue_) {
    parent_.continue_headers_ = std::move(headers);
    parent_.encode100ContinueHeaders(nullptr, *parent_.continue_headers_);
  }
}

void ActiveStreamDecoderFilter::encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) {
  parent_.response_headers_ = std::move(headers);
  parent_.encodeHeaders(nullptr, *parent_.response_headers_, end_stream);
}

void ActiveStreamDecoderFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  parent_.encodeData(nullptr, data, end_stream,
                     FilterManager::FilterIterationStartState::CanStartFromCurrent);
}

void ActiveStreamDecoderFilter::encodeTrailers(ResponseTrailerMapPtr&& trailers) {
  parent_.response_trailers_ = std::move(trailers);
  parent_.encodeTrailers(nullptr, *parent_.response_trailers_);
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

void FilterManager::addStreamDecoderFilterWorker(StreamDecoderFilterSharedPtr filter,
                                                 bool dual_filter) {
  ActiveStreamDecoderFilterPtr wrapper(new ActiveStreamDecoderFilter(*this, filter, dual_filter));
  filter->setDecoderFilterCallbacks(*wrapper);
  // Note: configured decoder filters are appended to decoder_filters_.
  // This means that if filters are configured in the following order (assume all three filters are
  // both decoder/encoder filters):
  //   http_filters:
  //     - A
  //     - B
  //     - C
  // The decoder filter chain will iterate through filters A, B, C.
  LinkedList::moveIntoListBack(std::move(wrapper), decoder_filters_);
}

void FilterManager::addStreamEncoderFilterWorker(StreamEncoderFilterSharedPtr filter,
                                                 bool dual_filter) {
  ActiveStreamEncoderFilterPtr wrapper(new ActiveStreamEncoderFilter(*this, filter, dual_filter));
  filter->setEncoderFilterCallbacks(*wrapper);
  // Note: configured encoder filters are prepended to encoder_filters_.
  // This means that if filters are configured in the following order (assume all three filters are
  // both decoder/encoder filters):
  //   http_filters:
  //     - A
  //     - B
  //     - C
  // The encoder filter chain will iterate through filters C, B, A.
  LinkedList::moveIntoList(std::move(wrapper), encoder_filters_);
}

void FilterManager::addAccessLogHandler(AccessLog::InstanceSharedPtr handler) {
  access_log_handlers_.push_back(handler);
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
    (*entry)->end_stream_ = state_.decoding_headers_only_ ||
                            (end_stream && continue_data_entry == decoder_filters_.end());
    FilterHeadersStatus status = (*entry)->decodeHeaders(headers, (*entry)->end_stream_);

    ASSERT(!(status == FilterHeadersStatus::ContinueAndEndStream && (*entry)->end_stream_));
    state_.filter_call_state_ &= ~FilterCallState::DecodeHeaders;
    ENVOY_STREAM_LOG(trace, "decode headers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

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

    (*entry)->decode_headers_called_ = true;
    if (!(*entry)->commonHandleAfterHeadersCallback(status, state_.decoding_headers_only_) &&
        std::next(entry) != decoder_filters_.end()) {
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

  // If we previously decided to decode only the headers, do nothing here.
  if (state_.decoding_headers_only_) {
    return;
  }

  // If a response is complete or a reset has been sent, filters do not care about further body
  // data. Just drop it.
  if (state_.local_complete_) {
    return;
  }

  auto trailers_added_entry = decoder_filters_.end();
  const bool trailers_exists_at_start = request_trailers_ != nullptr;
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
    (*entry)->end_stream_ = end_stream && !request_trailers_;
    FilterDataStatus status = (*entry)->handle_->decodeData(data, (*entry)->end_stream_);
    if ((*entry)->end_stream_) {
      (*entry)->handle_->decodeComplete();
    }
    state_.filter_call_state_ &= ~FilterCallState::DecodeData;
    if (end_stream) {
      state_.filter_call_state_ &= ~FilterCallState::LastDataFrame;
    }
    ENVOY_STREAM_LOG(trace, "decode data called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    processNewlyAddedMetadata();

    if (!trailers_exists_at_start && request_trailers_ &&
        trailers_added_entry == decoder_filters_.end()) {
      trailers_added_entry = entry;
    }

    if (!(*entry)->commonHandleAfterDataCallback(status, data, state_.decoder_filters_streaming_) &&
        std::next(entry) != decoder_filters_.end()) {
      // Stop iteration IFF this is not the last filter. If it is the last filter, continue with
      // processing since we need to handle the case where a terminal filter wants to buffer, but
      // a previous filter has added trailers.
      return;
    }
  }

  // If trailers were adding during decodeData we need to trigger decodeTrailers in order
  // to allow filters to process the trailers.
  if (trailers_added_entry != decoder_filters_.end()) {
    decodeTrailers(trailers_added_entry->get(), *request_trailers_);
  }

  if (end_stream) {
    disarmRequestTimeout();
  }
}

RequestTrailerMap& FilterManager::addDecodedTrailers() {
  // Trailers can only be added during the last data frame (i.e. end_stream = true).
  ASSERT(state_.filter_call_state_ & FilterCallState::LastDataFrame);

  // Trailers can only be added once.
  ASSERT(!request_trailers_);

  request_trailers_ = RequestTrailerMapImpl::create();
  return *request_trailers_;
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
    // TODO(mattklein123): Formalize error handling for filters and add tests. Should probably
    // throw an exception here.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
}

MetadataMapVector& FilterManager::addDecodedMetadata() { return *getRequestMetadataMapVector(); }

void FilterManager::decodeTrailers(ActiveStreamDecoderFilter* filter, RequestTrailerMap& trailers) {
  // If we previously decided to decode only the headers, do nothing here.
  if (state_.decoding_headers_only_) {
    return;
  }

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
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

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
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status),
                     metadata_map);
  }
}

void FilterManager::maybeEndDecode(bool end_stream) {
  ASSERT(!state_.remote_complete_);
  state_.remote_complete_ = end_stream;
  if (end_stream) {
    stream_info_.onLastDownstreamRxByteReceived();
    ENVOY_STREAM_LOG(debug, "request end stream", *this);
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

void FilterManager::sendLocalReply(
    bool is_grpc_request, Code code, absl::string_view body,
    const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  const bool is_head_request = state_.is_head_request_;
  stream_info_.setResponseCodeDetails(details);

  filter_manager_callbacks_.onLocalReply(code);

  if (response_headers_ == nullptr) {
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
    stream_info_.setResponseCodeDetails(details);
    // If we land in this branch, response headers have already been sent to the client.
    // All we can do at this point is reset the stream.
    ENVOY_STREAM_LOG(debug, "Resetting stream due to {}. Prior headers have already been sent",
                     *this, details);
    // TODO(snowp): This means we increment the tx_reset stat which we weren't doing previously.
    // Intended?
    filter_manager_callbacks_.resetStream();
  }
}

void FilterManager::sendLocalReplyViaFilterChain(
    bool is_grpc_request, Code code, absl::string_view body,
    const std::function<void(ResponseHeaderMap& headers)>& modify_headers, bool is_head_request,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status, absl::string_view details) {
  ENVOY_STREAM_LOG(debug, "Sending local reply with details {}", *this, details);
  ASSERT(response_headers_ == nullptr);
  // For early error handling, do a best-effort attempt to create a filter chain
  // to ensure access logging. If the filter chain already exists this will be
  // a no-op.
  createFilterChain();

  Utility::sendLocalReply(
      state_.destroyed_,
      Utility::EncodeFunctions{
          modify_headers,
          [this](ResponseHeaderMap& response_headers, Code& code, std::string& body,
                 absl::string_view& content_type) -> void {
            local_reply_.rewrite(request_headers_.get(), response_headers, stream_info_, code, body,
                                 content_type);
          },
          [this, modify_headers](ResponseHeaderMapPtr&& headers, bool end_stream) -> void {
            response_headers_ = std::move(headers);
            // TODO: Start encoding from the last decoder filter that saw the
            // request instead.
            encodeHeaders(nullptr, *response_headers_, end_stream);
          },
          [this](Buffer::Instance& data, bool end_stream) -> void {
            // TODO: Start encoding from the last decoder filter that saw the
            // request instead.
            encodeData(nullptr, data, end_stream,
                       FilterManager::FilterIterationStartState::CanStartFromCurrent);
          }},
      Utility::LocalReplyData{is_grpc_request, code, body, grpc_status, is_head_request});
}

void FilterManager::sendDirectLocalReply(
    Code code, absl::string_view body,
    const std::function<void(ResponseHeaderMap&)>& modify_headers, bool is_head_request,
    const absl::optional<Grpc::Status::GrpcStatus> grpc_status) {
  // Make sure we won't end up with nested watermark calls from the body buffer.
  state_.encoder_filters_streaming_ = true;
  Http::Utility::sendLocalReply(
      state_.destroyed_,
      Utility::EncodeFunctions{
          modify_headers,
          [&](ResponseHeaderMap& response_headers, Code& code, std::string& body,
              absl::string_view& content_type) -> void {
            local_reply_.rewrite(request_headers_.get(), response_headers, stream_info_, code, body,
                                 content_type);
          },
          [&](ResponseHeaderMapPtr&& response_headers, bool end_stream) -> void {
            // Move the response headers into the FilterManager to make sure they're visible to
            // access logs.
            response_headers_ = std::move(response_headers);

            state_.non_100_response_headers_encoded_ = true;
            filter_manager_callbacks_.encodeHeaders(*response_headers_, end_stream);
            maybeEndEncode(end_stream);
          },
          [&](Buffer::Instance& data, bool end_stream) -> void {
            filter_manager_callbacks_.encodeData(data, end_stream);
            maybeEndEncode(end_stream);
          }},
      Utility::LocalReplyData{Grpc::Common::hasGrpcContentType(*request_headers_), code, body,
                              grpc_status, is_head_request});
  maybeEndEncode(state_.local_complete_);
}

void FilterManager::encode100ContinueHeaders(ActiveStreamEncoderFilter* filter,
                                             ResponseHeaderMap& headers) {
  filter_manager_callbacks_.resetIdleTimer();
  ASSERT(proxy_100_continue_);
  // The caller must guarantee that encode100ContinueHeaders() is invoked at most once.
  ASSERT(!state_.has_continue_headers_ || filter != nullptr);
  // Make sure commonContinue continues encode100ContinueHeaders.
  state_.has_continue_headers_ = true;

  // Similar to the block in encodeHeaders, run encode100ContinueHeaders on each
  // filter. This is simpler than that case because 100 continue implies no
  // end-stream, and because there are normal headers coming there's no need for
  // complex continuation logic.
  // 100-continue filter iteration should always start with the next filter if available.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, false, FilterIterationStartState::AlwaysStartFromNext);
  for (; entry != encoder_filters_.end(); entry++) {
    ASSERT(!(state_.filter_call_state_ & FilterCallState::Encode100ContinueHeaders));
    state_.filter_call_state_ |= FilterCallState::Encode100ContinueHeaders;
    FilterHeadersStatus status = (*entry)->handle_->encode100ContinueHeaders(headers);
    state_.filter_call_state_ &= ~FilterCallState::Encode100ContinueHeaders;
    ENVOY_STREAM_LOG(trace, "encode 100 continue headers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfter100ContinueHeadersCallback(status)) {
      return;
    }
  }

  filter_manager_callbacks_.encode100ContinueHeaders(headers);
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
    (*entry)->end_stream_ = state_.encoding_headers_only_ ||
                            (end_stream && continue_data_entry == encoder_filters_.end());
    FilterHeadersStatus status = (*entry)->handle_->encodeHeaders(headers, (*entry)->end_stream_);
    if ((*entry)->end_stream_) {
      (*entry)->handle_->encodeComplete();
    }
    state_.filter_call_state_ &= ~FilterCallState::EncodeHeaders;
    ENVOY_STREAM_LOG(trace, "encode headers called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    (*entry)->encode_headers_called_ = true;
    const auto continue_iteration =
        (*entry)->commonHandleAfterHeadersCallback(status, state_.encoding_headers_only_);

    // If we're encoding a headers only response, then mark the local as complete. This ensures
    // that we don't attempt to reset the downstream request in doEndStream.
    if (state_.encoding_headers_only_) {
      state_.local_complete_ = true;
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

  const bool modified_end_stream = state_.encoding_headers_only_ ||
                                   (end_stream && continue_data_entry == encoder_filters_.end());
  state_.non_100_response_headers_encoded_ = true;
  filter_manager_callbacks_.encodeHeaders(headers, modified_end_stream);
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
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
  }
  // TODO(soya3129): update stats with metadata.

  // Now encode metadata via the codec.
  if (!metadata_map_ptr->empty()) {
    MetadataMapVector metadata_map_vector;
    metadata_map_vector.emplace_back(std::move(metadata_map_ptr));
    filter_manager_callbacks_.encodeMetadata(metadata_map_vector);
  }
}

ResponseTrailerMap& FilterManager::addEncodedTrailers() {
  // Trailers can only be added during the last data frame (i.e. end_stream = true).
  ASSERT(state_.filter_call_state_ & FilterCallState::LastDataFrame);

  // Trailers can only be added once.
  ASSERT(!response_trailers_);

  response_trailers_ = ResponseTrailerMapImpl::create();
  return *response_trailers_;
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
    // TODO(mattklein123): Formalize error handling for filters and add tests. Should probably
    // throw an exception here.
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
}

void FilterManager::encodeData(ActiveStreamEncoderFilter* filter, Buffer::Instance& data,
                               bool end_stream,
                               FilterIterationStartState filter_iteration_start_state) {
  filter_manager_callbacks_.resetIdleTimer();

  // If we previously decided to encode only the headers, do nothing here.
  if (state_.encoding_headers_only_) {
    return;
  }

  // Filter iteration may start at the current filter.
  std::list<ActiveStreamEncoderFilterPtr>::iterator entry =
      commonEncodePrefix(filter, end_stream, filter_iteration_start_state);
  auto trailers_added_entry = encoder_filters_.end();

  const bool trailers_exists_at_start = response_trailers_ != nullptr;
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

    (*entry)->end_stream_ = end_stream && !response_trailers_;
    FilterDataStatus status = (*entry)->handle_->encodeData(data, (*entry)->end_stream_);
    if ((*entry)->end_stream_) {
      (*entry)->handle_->encodeComplete();
    }
    state_.filter_call_state_ &= ~FilterCallState::EncodeData;
    if (end_stream) {
      state_.filter_call_state_ &= ~FilterCallState::LastDataFrame;
    }
    ENVOY_STREAM_LOG(trace, "encode data called: filter={} status={}", *this,
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));

    if (!trailers_exists_at_start && response_trailers_ &&
        trailers_added_entry == encoder_filters_.end()) {
      trailers_added_entry = entry;
    }

    if (!(*entry)->commonHandleAfterDataCallback(status, data, state_.encoder_filters_streaming_)) {
      return;
    }
  }

  const bool modified_end_stream = end_stream && trailers_added_entry == encoder_filters_.end();
  ASSERT(!state_.encoding_headers_only_);
  filter_manager_callbacks_.encodeData(data, modified_end_stream);
  maybeEndEncode(modified_end_stream);

  // If trailers were adding during encodeData we need to trigger decodeTrailers in order
  // to allow filters to process the trailers.
  if (trailers_added_entry != encoder_filters_.end()) {
    encodeTrailers(trailers_added_entry->get(), *response_trailers_);
  }
}

void FilterManager::encodeTrailers(ActiveStreamEncoderFilter* filter,
                                   ResponseTrailerMap& trailers) {
  filter_manager_callbacks_.resetIdleTimer();

  // If we previously decided to encode only the headers, do nothing here.
  if (state_.encoding_headers_only_) {
    return;
  }

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
                     static_cast<const void*>((*entry).get()), static_cast<uint64_t>(status));
    if (!(*entry)->commonHandleAfterTrailersCallback(status)) {
      return;
    }
  }

  filter_manager_callbacks_.encodeTrailers(trailers);
  maybeEndEncode(true);
}

void FilterManager::maybeEndEncode(bool end_stream) {
  if (end_stream) {
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

bool FilterManager::createFilterChain() {
  if (state_.created_filter_chain_) {
    return false;
  }
  bool upgrade_rejected = false;
  const HeaderEntry* upgrade = nullptr;
  if (request_headers_) {
    upgrade = request_headers_->Upgrade();

    // Treat CONNECT requests as a special upgrade case.
    if (!upgrade && HeaderUtility::isConnect(*request_headers_)) {
      upgrade = request_headers_->Method();
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

bool ActiveStreamDecoderFilter::recreateStream() {
  // Because the filter's and the HCM view of if the stream has a body and if
  // the stream is complete may differ, re-check bytesReceived() to make sure
  // there was no body from the HCM's point of view.
  if (!complete() || parent_.stream_info_.bytesReceived() != 0) {
    return false;
  }

  parent_.stream_info_.setResponseCodeDetails(
      StreamInfo::ResponseCodeDetails::get().InternalRedirect);

  parent_.filter_manager_callbacks_.recreateStream(std::move(parent_.request_headers_),
                                                   parent_.stream_info_.filter_state_);

  return true;
}

void ActiveStreamDecoderFilter::addUpstreamSocketOptions(
    const Network::Socket::OptionsSharedPtr& options) {

  Network::Socket::appendOptions(parent_.upstream_options_, options);
}

Network::Socket::OptionsSharedPtr ActiveStreamDecoderFilter::getUpstreamSocketOptions() const {
  return parent_.upstream_options_;
}

void ActiveStreamDecoderFilter::requestRouteConfigUpdate(
    Http::RouteConfigUpdatedCallbackSharedPtr route_config_updated_cb) {
  parent_.filter_manager_callbacks_.requestRouteConfigUpdate(dispatcher(),
                                                             std::move(route_config_updated_cb));
}

absl::optional<Router::ConfigConstSharedPtr> ActiveStreamDecoderFilter::routeConfig() {
  return parent_.filter_manager_callbacks_.routeConfig();
}

Buffer::WatermarkBufferPtr ActiveStreamEncoderFilter::createBuffer() {
  auto buffer = new Buffer::WatermarkBuffer(
      [this]() -> void { this->responseDataDrained(); },
      [this]() -> void { this->responseDataTooLarge(); },
      []() -> void { /* TODO(adisuissa): Handle overflow watermark */ });
  buffer->setWatermarks(parent_.buffer_limit_);
  return Buffer::WatermarkBufferPtr{buffer};
}
Buffer::WatermarkBufferPtr& ActiveStreamEncoderFilter::bufferedData() {
  return parent_.buffered_response_data_;
}
bool ActiveStreamEncoderFilter::complete() { return parent_.state_.local_complete_; }
bool ActiveStreamEncoderFilter::has100Continueheaders() {
  return parent_.state_.has_continue_headers_ && !continue_headers_continued_;
}
void ActiveStreamEncoderFilter::do100ContinueHeaders() {
  parent_.encode100ContinueHeaders(this, *parent_.continue_headers_);
}
void ActiveStreamEncoderFilter::doHeaders(bool end_stream) {
  parent_.encodeHeaders(this, *parent_.response_headers_, end_stream);
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
  parent_.encodeTrailers(this, *parent_.response_trailers_);
}
bool ActiveStreamEncoderFilter::hasTrailers() { return parent_.response_trailers_ != nullptr; }
void ActiveStreamEncoderFilter::addEncodedData(Buffer::Instance& data, bool streaming) {
  return parent_.addEncodedData(*this, data, streaming);
}

void ActiveStreamEncoderFilter::injectEncodedDataToFilterChain(Buffer::Instance& data,
                                                               bool end_stream) {
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

Http1StreamEncoderOptionsOptRef ActiveStreamEncoderFilter::http1StreamEncoderOptions() {
  // TODO(mattklein123): At some point we might want to actually wrap this interface but for now
  // we give the filter direct access to the encoder options.
  return parent_.filter_manager_callbacks_.http1StreamEncoderOptions();
}

void ActiveStreamEncoderFilter::responseDataTooLarge() {
  if (parent_.state_.encoder_filters_streaming_) {
    onEncoderFilterAboveWriteBufferHighWatermark();
  } else {
    parent_.filter_manager_callbacks_.onResponseDataTooLarge();

    // In this case, sendLocalReply will either send a response directly to the encoder, or
    // reset the stream.
    parent_.sendLocalReply(
        parent_.request_headers_ && Grpc::Common::isGrpcRequestHeaders(*parent_.request_headers_),
        Http::Code::InternalServerError, CodeUtility::toString(Http::Code::InternalServerError),
        nullptr, absl::nullopt, StreamInfo::ResponseCodeDetails::get().ResponsePayloadTooLarge);
  }
}

void ActiveStreamEncoderFilter::responseDataDrained() {
  onEncoderFilterBelowWriteBufferLowWatermark();
}

void ActiveStreamFilterBase::resetStream() { parent_.filter_manager_callbacks_.resetStream(); }

uint64_t ActiveStreamFilterBase::streamId() const { return parent_.streamId(); }

} // namespace Http
} // namespace Envoy
