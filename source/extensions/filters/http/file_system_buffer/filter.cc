#include "filter.h"
#include "source/extensions/filters/http/file_system_buffer/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

FileSystemBufferFilter::FileSystemBufferFilter(
    std::shared_ptr<FileSystemBufferFilterConfig> base_config)
    : base_config_(std::move(base_config)) {}

const std::string& FileSystemBufferFilter::filterName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.filters.http.file_system_buffer");
}

bool FileSystemBufferFilter::initPerRouteConfig() {
  if (config_) {
    return true;
  }
  auto route = request_callbacks_->route();
  FilterConfigVector config_chain;
  if (route) {
    // TODO(wbpcode): fix this to use the callbacks to get the route specific configs.
    for (const auto* cfg : route->perFilterConfigs(FileSystemBufferFilter::filterName())) {
      auto typed_cfg = dynamic_cast<const FileSystemBufferFilterConfig*>(cfg);
      if (typed_cfg) {
        config_chain.emplace_back(*typed_cfg);
      }
    }
  }
  config_chain.emplace_back(*base_config_);
  config_.emplace(config_chain);
  request_state_.setConfig(config_->request());
  response_state_.setConfig(config_->response());
  if (config_->request().behavior().bypass() && config_->response().behavior().bypass()) {
    // It's okay to not have an AsyncFileManager if the filter is bypassed.
    return true;
  }
  return config_->hasAsyncFileManager();
}

Http::FilterHeadersStatus FileSystemBufferFilter::onBadConfig() {
  filterError("config was missing manager_config");
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus FileSystemBufferFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                bool end_stream) {
  // If there is no body then we're done here.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }
  if (!initPerRouteConfig()) {
    return onBadConfig();
  }
  if (request_state_.config_->behavior().bypass()) {
    return Http::FilterHeadersStatus::Continue;
  }
  // If there's already a content length header we don't need to inject one unless the configuration
  // says specifically to overwrite any incoming content-length.
  request_state_.injecting_content_length_header_ =
      (request_state_.config_->behavior().injectContentLength() &&
       headers.ContentLength() == nullptr) ||
      request_state_.config_->behavior().replaceContentLength();
  // This effectively limits the output buffer to the size of the memory buffer, which should keep
  // us from just draining all the streaming data into that 'hidden' memory buffer. It unfortunately
  // means we still have the thread potentially using twice the memory that the user configured as
  // the memory limit, once in our own buffer and once in the outgoing buffer. (Plus overflow
  // because the limit isn't hard.)
  request_callbacks_->setDecoderBufferLimit(request_state_.config_->memoryBufferBytesLimit());
  request_headers_ = &headers;
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus FileSystemBufferFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                                bool end_stream) {
  // If there is no body then we're done here.
  if (end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }
  if (!initPerRouteConfig()) {
    return onBadConfig();
  }
  if (response_state_.config_->behavior().bypass()) {
    return Http::FilterHeadersStatus::Continue;
  }
  // If there's already a content length header we don't need to inject one unless the configuration
  // says specifically to overwrite any incoming content-length.
  response_state_.injecting_content_length_header_ =
      (response_state_.config_->behavior().injectContentLength() &&
       headers.ContentLength() == nullptr) ||
      response_state_.config_->behavior().replaceContentLength();
  // This effectively limits the output buffer to the size of the memory buffer, which should keep
  // us from just draining all the streaming data into that 'hidden' memory buffer. It unfortunately
  // means we still have the thread potentially using twice the memory that the user configured as
  // the memory limit, once in our own buffer and once in the outgoing buffer. (Plus overflow
  // because the limit isn't hard.)
  response_callbacks_->setEncoderBufferLimit(response_state_.config_->memoryBufferBytesLimit());
  response_headers_ = &headers;
  return Http::FilterHeadersStatus::StopIteration;
}

void FileSystemBufferFilter::onAboveWriteBufferHighWatermark() {
  if (response_state_.sending_watermark_) {
    response_state_.sending_watermark_ = false;
    return;
  }
  response_state_.water_level_++;
  ENVOY_STREAM_LOG(debug, "received high watermark", *response_callbacks_);
}

void FileSystemBufferFilter::onBelowWriteBufferLowWatermark() {
  if (response_state_.sending_watermark_) {
    response_state_.sending_watermark_ = false;
    return;
  }
  ASSERT(response_state_.water_level_ > 0);
  response_state_.water_level_--;
  ENVOY_STREAM_LOG(debug, "received low watermark", *response_callbacks_);
  if (response_state_.water_level_ == 0) {
    dispatchStateChanged();
  }
}

void FileSystemBufferFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  request_callbacks_ = &callbacks;
  request_callbacks_->addDownstreamWatermarkCallbacks(*this);
}

void FileSystemBufferFilter::filterError(absl::string_view err) {
  aborted_ = true;
  ENVOY_STREAM_LOG(error, "{}", *request_callbacks_, err);
  request_callbacks_->sendLocalReply(Http::Code::InternalServerError, "buffer filter error",
                                     nullptr, absl::nullopt, "");
}

Http::FilterDataStatus FileSystemBufferFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (request_state_.memory_used_ + request_state_.storage_consumed_ + data.length() >
      request_state_.config_->memoryBufferBytesLimit() +
          request_state_.config_->storageBufferBytesLimit()) {
    if (request_state_.injecting_content_length_header_ ||
        request_state_.config_->behavior().alwaysFullyBuffer()) {
      request_callbacks_->sendLocalReply(Http::Code::PayloadTooLarge, "buffer limit exceeded",
                                         nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }
  return receiveData(request_state_, data, end_stream);
}

Http::FilterDataStatus FileSystemBufferFilter::receiveData(BufferedStreamState& state,
                                                           Buffer::Instance& data,
                                                           bool end_stream) {
  if (state.config_->behavior().bypass()) {
    return Http::FilterDataStatus::Continue;
  }
  state.memory_used_ += data.length();
  while (data.length() > state.config_->memoryBufferBytesLimit()) {
    state.buffer_.push_back(
        std::make_unique<Fragment>(data, state.config_->memoryBufferBytesLimit()));
  }
  state.buffer_.push_back(std::make_unique<Fragment>(data));
  if (end_stream) {
    state.seen_end_stream_ = true;
  }
  dispatchStateChanged();
  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus FileSystemBufferFilter::receiveTrailers(const BufferBehavior& behavior,
                                                                   BufferedStreamState& state) {
  // If seen_end_stream_ was populated via receiveData then we shouldn't even get here since that
  // means there are no trailers. If it was populated by a previous call to receiveTrailers then
  // that means we must have called continueDecoding or continueEncoding, or we wouldn't get here
  // again, and we only do that when the data buffer has been flushed, at which point it's time to
  // deliver any trailers.
  if (state.seen_end_stream_ || behavior.bypass()) {
    return Http::FilterTrailersStatus::Continue;
  }
  state.seen_end_stream_ = true;
  dispatchStateChanged();
  return Http::FilterTrailersStatus::StopIteration;
}

Http::FilterTrailersStatus FileSystemBufferFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  return receiveTrailers(response_state_.config_->behavior(), response_state_);
}

Http::FilterDataStatus FileSystemBufferFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (response_state_.memory_used_ + response_state_.storage_consumed_ + data.length() >
      response_state_.config_->memoryBufferBytesLimit() +
          response_state_.config_->storageBufferBytesLimit()) {
    if (response_state_.injecting_content_length_header_ ||
        response_state_.config_->behavior().alwaysFullyBuffer()) {
      response_callbacks_->sendLocalReply(Http::Code::InternalServerError, "buffer limit exceeded",
                                          nullptr, absl::nullopt, "");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
  }
  return receiveData(response_state_, data, end_stream);
}

Http::FilterTrailersStatus FileSystemBufferFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return receiveTrailers(request_state_.config_->behavior(), request_state_);
}

Http::Filter1xxHeadersStatus FileSystemBufferFilter::encode1xxHeaders(Http::ResponseHeaderMap&) {
  return Http::Filter1xxHeadersStatus::Continue;
}

Http::FilterMetadataStatus FileSystemBufferFilter::encodeMetadata(Http::MetadataMap&) {
  return Http::FilterMetadataStatus::Continue;
}

void FileSystemBufferFilter::setEncoderFilterCallbacks(
    Http::StreamEncoderFilterCallbacks& callbacks) {
  response_callbacks_ = &callbacks;
}

void FileSystemBufferFilter::onDestroy() {
  *is_destroyed_ = true;
  if (cancel_in_flight_async_action_) {
    cancel_in_flight_async_action_();
  }
  request_state_.close();
  response_state_.close();
}

void FileSystemBufferFilter::maybeOutputRequest() {
  if (request_state_.finished_) {
    return;
  }
  if (request_state_.seen_end_stream_ ||
      (!request_state_.injecting_content_length_header_ &&
       !request_state_.config_->behavior().alwaysFullyBuffer())) {
    if (request_state_.injecting_content_length_header_) {
      request_headers_->setContentLength(request_state_.bufferSize());
      request_state_.injecting_content_length_header_ = false;
    }

    while (!request_state_.buffer_.empty() && request_state_.buffer_.front()->isMemory() &&
           request_state_.water_level_ == 0) {
      request_state_.memory_used_ -= request_state_.buffer_.front()->size();
      auto out = request_state_.buffer_.front()->extract();
      request_state_.buffer_.pop_front();
      request_callbacks_->injectDecodedDataToFilterChain(*out, false);
    }
  }
  if (request_state_.buffer_.empty() && request_state_.seen_end_stream_) {
    request_state_.finished_ = true;
    request_callbacks_->continueDecoding();
  }
}

bool FileSystemBufferFilter::maybeOutputResponse() {
  if (response_state_.finished_) {
    return true;
  }
  if (response_state_.seen_end_stream_ ||
      (!response_state_.injecting_content_length_header_ &&
       !response_state_.config_->behavior().alwaysFullyBuffer())) {
    if (response_state_.injecting_content_length_header_) {
      response_headers_->setContentLength(response_state_.bufferSize());
      response_state_.injecting_content_length_header_ = false;
    }
    while (!response_state_.buffer_.empty() && response_state_.buffer_.front()->isMemory() &&
           response_state_.water_level_ == 0) {
      response_state_.memory_used_ -= response_state_.buffer_.front()->size();
      auto out = response_state_.buffer_.front()->extract();
      response_state_.buffer_.pop_front();
      response_callbacks_->injectEncodedDataToFilterChain(*out, false);
    }
  }
  if (response_state_.buffer_.empty() && response_state_.seen_end_stream_) {
    response_state_.finished_ = true;
    response_callbacks_->continueEncoding();
    return true;
  }
  return false;
}

void BufferedStreamState::close() {
  if (async_file_handle_) {
    auto queued = async_file_handle_->close(nullptr, [](absl::Status) {});
    ASSERT(queued.ok());
  }
}

void BufferedStreamState::setConfig(
    const FileSystemBufferFilterMergedConfig::StreamConfig& config) {
  config_ = &config;
}

// If there's more in memory than there should be, and we can do anything about it, we should ask
// the source to stop sending while we do that.
bool BufferedStreamState::shouldSendHighWatermark() const {
  return !sent_slow_down_ && memory_used_ > config_->memoryBufferBytesLimit() +
                                                config_->storageBufferQueueHighWatermarkBytes();
}

// If we're back below a lower memory threshold, or we can't move anything more to disk without
// receiving more, and we have previously asked the source to stop sending, we can tell it it's okay
// to go again.
bool BufferedStreamState::shouldSendLowWatermark() const {
  return sent_slow_down_ && memory_used_ < config_->memoryBufferBytesLimit() +
                                               config_->storageBufferQueueHighWatermarkBytes() / 2;
}

bool FileSystemBufferFilter::maybeSendWatermarkUpdates() {
  if (request_state_.shouldSendHighWatermark()) {
    request_state_.sending_watermark_ = true;
    ENVOY_STREAM_LOG(debug, "sending high watermark for request", *request_callbacks_);
    request_callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
    request_state_.sent_slow_down_ = true;
  } else if (request_state_.shouldSendLowWatermark()) {
    request_state_.sending_watermark_ = true;
    ENVOY_STREAM_LOG(debug, "sending low watermark for request", *request_callbacks_);
    request_callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
    request_state_.sent_slow_down_ = false;
    if (request_state_.water_level_ == 0) {
      return true;
    }
  }
  if (response_state_.shouldSendHighWatermark()) {
    response_state_.sending_watermark_ = true;
    ENVOY_STREAM_LOG(debug, "sending high watermark for response", *response_callbacks_);
    response_callbacks_->onEncoderFilterAboveWriteBufferHighWatermark();
    response_state_.sent_slow_down_ = true;
  } else if (response_state_.shouldSendLowWatermark()) {
    response_state_.sending_watermark_ = true;
    ENVOY_STREAM_LOG(debug, "sending low watermark for response", *response_callbacks_);
    response_callbacks_->onEncoderFilterBelowWriteBufferLowWatermark();
    response_state_.sent_slow_down_ = false;
    if (response_state_.water_level_ == 0) {
      return true;
    }
  }
  return false;
}

void FileSystemBufferFilter::onStateChange() {
  if (aborted_) {
    return;
  }
  maybeOutputRequest();
  if (maybeOutputResponse()) {
    // If maybeOutputResponse returned true that means the filter is finished and there's no more
    // work to do. The filter might also be destroyed in this case, so we must stop work
    // immediately.
    return;
  }
  if (maybeSendWatermarkUpdates()) {
    return onStateChange();
  }
  // We should only queue a storage move if no disk action is already in flight.
  if (!cancel_in_flight_async_action_) {
    maybeStorage(request_state_, *request_callbacks_) ||
        maybeStorage(response_state_, *response_callbacks_);
  }
}

bool FileSystemBufferFilter::maybeStorage(BufferedStreamState& state,
                                          Http::StreamFilterCallbacks& callbacks) {
  // If there's enough space in the memory buffer to pull something out of storage, or no choice but
  // to pull something out of storage, do that.
  if (state.storage_used_ > 0 && (state.memory_used_ < state.config_->memoryBufferBytesLimit() ||
                                  (*state.buffer_.begin())->isStorage())) {
    auto earliest_storage_fragment = std::find_if(state.buffer_.begin(), state.buffer_.end(),
                                                  [](auto& p) { return p->isStorage(); });
    size_t size = (**earliest_storage_fragment).size();
    if (size + state.memory_used_ < state.config_->memoryBufferBytesLimit() ||
        earliest_storage_fragment == state.buffer_.begin()) {
      state.storage_used_ -= size;
      state.memory_used_ += size;
      ENVOY_STREAM_LOG(debug, "retrieving buffer fragment (size={}) from storage", callbacks, size);
      auto queued = (**earliest_storage_fragment)
                        .fromStorage(state.async_file_handle_, request_callbacks_->dispatcher(),
                                     getOnFileActionCompleted());
      ASSERT(queued.ok());
      cancel_in_flight_async_action_ = std::move(queued.value());
      return true;
    }
  }
  // If there's more data in memory than there should be, put some into storage.
  if (state.memory_used_ > state.config_->memoryBufferBytesLimit() &&
      state.storage_used_ < state.config_->storageBufferBytesLimit()) {
    if (!state.async_file_handle_) {
      // File isn't open yet - open it and then check again if we still need to store data.
      ENVOY_STREAM_LOG(debug, "memory buffer exceeded - creating buffer file", callbacks);
      // The callback won't be called if the filter was destroyed, and if the file was
      // racily created it will be closed.
      cancel_in_flight_async_action_ = config_->asyncFileManager().createAnonymousFile(
          &request_callbacks_->dispatcher(), config_->storageBufferPath(),
          [this, &state](absl::StatusOr<AsyncFileHandle> file_handle) {
            if (!file_handle.ok()) {
              filterError(fmt::format("{} failed to create buffer file: {}", filterName(),
                                      file_handle.status().ToString()));
              return;
            }
            state.async_file_handle_ = std::move(file_handle.value());
            cancel_in_flight_async_action_ = nullptr;
            onStateChange();
          });
      return true;
    }
    auto latest_memory_fragment = std::find_if(state.buffer_.rbegin(), state.buffer_.rend(),
                                               [](auto& p) { return p->isMemory(); });
    Fragment* fragment = latest_memory_fragment->get();
    auto size = fragment->size();
    state.storage_used_ += size;
    state.storage_consumed_ += size;
    state.memory_used_ -= size;
    ENVOY_STREAM_LOG(debug, "sending buffer fragment (size={}) to storage", callbacks, size);
    auto to_storage =
        fragment->toStorage(state.async_file_handle_, state.storage_offset_,
                            request_callbacks_->dispatcher(), getOnFileActionCompleted());
    ASSERT(to_storage.ok());
    cancel_in_flight_async_action_ = std::move(to_storage.value());
    state.storage_offset_ += size;
    return true;
  }
  return false;
}

absl::AnyInvocable<void(absl::Status)> FileSystemBufferFilter::getOnFileActionCompleted() {
  // This callback is aborted in onDestroy, so is safe to capture 'this' -
  // it won't be called if the filter has been deleted.
  return [this](absl::Status status) {
    cancel_in_flight_async_action_ = nullptr;
    if (status.ok()) {
      onStateChange();
    } else {
      filterError(fmt::format("{} file action failed: {}", filterName(), status.ToString()));
    }
  };
}

void FileSystemBufferFilter::safeDispatch(absl::AnyInvocable<void()> fn) {
  request_callbacks_->dispatcher().post(
      [is_destroyed = is_destroyed_, fn = std::move(fn)]() mutable {
        if (!*is_destroyed) {
          std::move(fn)();
        }
      });
}

void FileSystemBufferFilter::dispatchStateChanged() {
  safeDispatch([this]() { onStateChange(); });
}

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
