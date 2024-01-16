#pragma once

#include <memory>
#include <string>

#include "source/extensions/filters/http/file_system_buffer/filter_config.h"
#include "source/extensions/filters/http/file_system_buffer/fragment.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

struct BufferedStreamState {
  const FileSystemBufferFilterMergedConfig::StreamConfig* config_;
  bool injecting_content_length_header_ = false;
  bool headers_sent_ = false;
  std::deque<std::unique_ptr<Fragment>> buffer_;
  bool seen_end_stream_ = false;
  bool sent_slow_down_ = false;
  bool finished_ = false;
  // This bool is used to signify that we should *not* intercept
  // or record a watermark event because it was this filter trying to send
  // that event.
  bool sending_watermark_ = false;
  size_t water_level_ = 0;
  size_t memory_used_ = 0;
  off_t storage_offset_ = 0;
  // The amount of data in the buffer that can be retrieved.
  size_t storage_used_ = 0;
  // The total amount of data written to the storage buffer. Since the current
  // implementation currently only appends to the buffer file and does not reuse
  // freed storage, the storage limit can be reached even if the buffer was drained.
  size_t storage_consumed_ = 0;
  AsyncFileHandle async_file_handle_;

  size_t bufferSize() const { return memory_used_ + storage_offset_; }
  bool shouldSendHighWatermark() const;
  bool shouldSendLowWatermark() const;
  void setConfig(const FileSystemBufferFilterMergedConfig::StreamConfig& config);
  void close();
};

class FileSystemBufferFilter : public Http::StreamFilter,
                               public Http::DownstreamWatermarkCallbacks,
                               public Logger::Loggable<Logger::Id::http2> {
public:
  explicit FileSystemBufferFilter(std::shared_ptr<FileSystemBufferFilterConfig> base_config);

  static const std::string& filterName();

  // Http::StreamFilter
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  void onDestroy() override;

private:
  // This is captured so that callbacks can alter their behavior (and avoid nullptr)
  // if the filter has been destroyed since the callback was queued.
  // We use this rather than shared_from_this because onDestroy happens potentially
  // significantly earlier than the destructor, and we want to abort callbacks after
  // onDestroy.
  std::shared_ptr<bool> is_destroyed_ = std::make_shared<bool>(false);
  // Merges any per-route config with the default config. Returns false if the config lacked
  // a file manager.
  bool initPerRouteConfig();
  Http::FilterHeadersStatus onBadConfig();
  Http::FilterDataStatus receiveData(BufferedStreamState& state, Buffer::Instance& data,
                                     bool end_stream);
  Http::FilterTrailersStatus receiveTrailers(const BufferBehavior& behavior,
                                             BufferedStreamState& state);

  // onStateChange makes stateful decisions about what to do next.
  // Depending on the state of buffers and streams, may do one or more of the 'maybe' functions
  // below.
  void onStateChange();

  // Conditional operations to perform on state change.
  // If the server is ready to receive, sends any request data from the front of the buffer.
  void maybeOutputRequest();
  // If the client is ready to receive, sends any response data from the front of the buffer.
  // If this finishes the connection, returns true.
  bool maybeOutputResponse();
  // If the buffer has too much queued for writing to disk, asks the source to slow down.
  // If a low watermark was sent (that might require us to do another action), returns true.
  bool maybeSendWatermarkUpdates();
  // If the memory buffer is overfull, moves the *latest* chunks from memory to storage.
  // If the memory buffer has room, moves the *earliest* chunks from storage to memory.
  // These operations are asynchronous; the impacted buffer fragments are in an unusable state until
  // the operation completes.
  bool maybeStorage(BufferedStreamState& state, Http::StreamFilterCallbacks& callbacks);
  std::function<void(absl::Status)> getOnFileActionCompleted();

  // Called if an unrecoverable error occurs in the filter (e.g. a file operation fails). Internal
  // server error.
  void filterError(absl::string_view err);

  // Returns a safe dispatch function that aborts if the filter has been destroyed.
  std::function<void(std::function<void()>)> getSafeDispatch();

  // Queue an onStateChange in the dispatcher. This is used to get the next piece of work back
  // into the Envoy thread from an AsyncFiles thread, or to queue work that may not be allowed
  // in the current Envoy context (e.g. updating watermark callbacks during a watermark callback
  // is not allowed, injecting data to a stream during decodeData is not allowed).
  void dispatchStateChanged();

  BufferedStreamState request_state_;
  BufferedStreamState response_state_;
  Http::RequestHeaderMap* request_headers_ = nullptr;
  Http::ResponseHeaderMap* response_headers_ = nullptr;

  const std::shared_ptr<FileSystemBufferFilterConfig> base_config_;
  // config_ is a merge of the base config and any per-route config.
  absl::optional<FileSystemBufferFilterMergedConfig> config_;
  Http::StreamDecoderFilterCallbacks* request_callbacks_ = nullptr;
  Http::StreamEncoderFilterCallbacks* response_callbacks_ = nullptr;
  bool aborted_ = false;

  CancelFunction cancel_in_flight_async_action_;

  friend class FileSystemBufferFilterConfigTest; // to inspect base_config_
  friend class FileSystemBufferFilterTest;       // to inspect config_
};

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
