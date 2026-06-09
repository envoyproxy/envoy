#pragma once

#include <memory>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/file_server/file_streamer.h"
#include "source/extensions/filters/http/file_server/filter_config.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

class FileServerFilter : public Http::PassThroughDecoderFilter,
                         public FileStreamerClient,
                         public Http::DownstreamWatermarkCallbacks {
public:
  explicit FileServerFilter(std::shared_ptr<const FileServerConfig> file_server_config)
      : file_server_config_(file_server_config), file_streamer_(*this) {}

  static const std::string& filterName();

  void errorFromFile(Http::Code code, absl::string_view log_message) override;
  bool headersFromFile(Http::ResponseHeaderMapPtr response_headers) override;
  void bodyChunkFromFile(Buffer::InstancePtr buf, bool end_stream) override;

  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  void onDestroy() override;
  // Http::DownstreamWatermarkCallbacks
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

private:
  std::shared_ptr<const FileServerConfig> file_server_config_;
  friend class FileServerConfigTest; // Allow test access to file_server_config_.
  FileStreamer file_streamer_;
  bool is_head_ = false;
  bool headers_sent_ = false;
};

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
