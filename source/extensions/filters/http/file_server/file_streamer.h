#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"

#include "source/extensions/common/async_files/async_file_manager.h"
#include "source/extensions/filters/http/file_server/filter_config.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileServer {

using Extensions::Common::AsyncFiles::AsyncFileHandle;
using Extensions::Common::AsyncFiles::AsyncFileManager;
using Extensions::Common::AsyncFiles::CancelFunction;

class FileStreamerClient {
public:
  virtual void errorFromFile(Http::Code code, absl::string_view log_message) PURE;
  // Return true to keep going - false is for HEAD requests.
  virtual bool headersFromFile(Http::ResponseHeaderMapPtr response_headers) PURE;
  virtual void bodyChunkFromFile(Buffer::InstancePtr buf, bool end_stream) PURE;
  virtual ~FileStreamerClient() = default;
};

class FileStreamer {
public:
  explicit FileStreamer(FileStreamerClient& client) : client_(client) {}
  ~FileStreamer();
  // Starts reading and streaming the file.
  // end == 0 means read to end of file.
  void begin(const FileServerConfig& config, Event::Dispatcher& dispatcher, uint64_t start,
             uint64_t end, std::filesystem::path file_path);
  // Call when the downstream buffer is over watermark.
  // Stops at the completion of the current action if not unpaused first.
  void pause();
  // Call when the downstream buffer is under watermark.
  // Starts the next action if previously paused.
  void unpause();
  // Call when the filter is destroyed for whatever reason.
  void abort();

private:
  const FileServerConfig* file_server_config_;
  void startFile();
  void startDir(int behavior_index);
  void onFileOpened(AsyncFileHandle handle);
  void readBodyChunk();
  Event::Dispatcher* dispatcher_;
  FileStreamerClient& client_;
  std::filesystem::path file_path_;
  uint64_t pos_ = 0;
  // If zero, fetches entire file.
  // To get the last byte, end_ must be the size of the file, not the inclusive last byte
  // like a range request uses.
  uint64_t end_ = 0;
  bool paused_ = false;
  bool action_has_been_postponed_by_pause_ = false;
  AsyncFileHandle async_file_;
  CancelFunction cancel_callback_ = []() {};
};

} // namespace FileServer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
