#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/file_system_buffer/v3/file_system_buffer.pb.h"
#include "envoy/extensions/filters/http/file_system_buffer/v3/file_system_buffer.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/common/async_files/async_file_manager_factory.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

using Extensions::Common::AsyncFiles::AsyncFileManager;
using Extensions::Common::AsyncFiles::AsyncFileManagerFactory;
using ProtoBufferBehavior =
    envoy::extensions::filters::http::file_system_buffer::v3::BufferBehavior;
using ProtoStreamConfig = envoy::extensions::filters::http::file_system_buffer::v3::StreamConfig;
using ProtoFileSystemBufferFilterConfig =
    envoy::extensions::filters::http::file_system_buffer::v3::FileSystemBufferFilterConfig;

// A convenience representation of a BufferBehavior from the config proto. Each subtype maps to
// a subclass of BufferBehavior that returns the appropriate values for each of these functions.
class BufferBehavior {
public:
  virtual bool injectContentLength() const { return false; }
  virtual bool replaceContentLength() const { return false; }
  virtual bool alwaysFullyBuffer() const { return false; }
  virtual bool bypass() const { return false; }
  virtual ~BufferBehavior() = default;
};

// The raw proto config and captured file_system_thread_pool.
// This is intentionally unusable; filter will generate a FileSystemBufferFilterMergedConfig
// to merge the global config and any route/vhost configs.
class FileSystemBufferFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  explicit FileSystemBufferFilterConfig(
      std::shared_ptr<AsyncFileManagerFactory> factory,
      std::shared_ptr<Extensions::Common::AsyncFiles::AsyncFileManager> async_file_manager,
      const ProtoFileSystemBufferFilterConfig& config);

  const ProtoFileSystemBufferFilterConfig& proto() const { return config_; }
  const std::shared_ptr<AsyncFileManager>& asyncFileManager() const { return async_file_manager_; }

private:
  // FileManagerFactory is captured to prevent it from going out of scope while a dependent
  // AsyncFileManager* exists.
  const std::shared_ptr<AsyncFileManagerFactory> factory_;
  // async_file_manager_ may be null in any given config, but will throw an exception if it's
  // unset in all of route, vhost and listener-level configs.
  const std::shared_ptr<Extensions::Common::AsyncFiles::AsyncFileManager> async_file_manager_;
  const ProtoFileSystemBufferFilterConfig config_;
};

// To facilitate merging configuration quickly, we use an InlinedVector. The size of 4 was
// selected to accommodate listener, vhost, route, plus one to avoid a surprising cost
// increase if another layer is added in future (since it's short-lived and on the stack
// the cost of that +1 is essentially nothing.)
using StreamConfigVector = absl::InlinedVector<std::reference_wrapper<const ProtoStreamConfig>, 4>;
using FilterConfigVector =
    absl::InlinedVector<std::reference_wrapper<const FileSystemBufferFilterConfig>, 4>;

class FileSystemBufferFilterMergedConfig {
public:
  class StreamConfig {
  public:
    StreamConfig(const StreamConfigVector& configs, bool has_file_manager);
    size_t memoryBufferBytesLimit() const { return memory_buffer_bytes_limit_; }
    size_t storageBufferBytesLimit() const { return storage_buffer_bytes_limit_; }
    size_t storageBufferQueueHighWatermarkBytes() const {
      return storage_buffer_queue_high_watermark_bytes_;
    }
    const BufferBehavior& behavior() const { return behavior_; }

  private:
    const size_t memory_buffer_bytes_limit_;
    const size_t storage_buffer_bytes_limit_;
    const size_t storage_buffer_queue_high_watermark_bytes_;
    const BufferBehavior& behavior_;
  };
  // The first config is highest priority, overriding later configs for any value present.
  explicit FileSystemBufferFilterMergedConfig(const FilterConfigVector& configs);
  const std::string& storageBufferPath() const { return storage_buffer_path_; }
  bool hasAsyncFileManager() const { return async_file_manager_ != nullptr; }
  AsyncFileManager& asyncFileManager() const { return *async_file_manager_; }
  const StreamConfig& request() const { return request_; }
  const StreamConfig& response() const { return response_; }

  static constexpr size_t default_memory_buffer_bytes_limit = 1024 * 1024;
  static constexpr size_t default_storage_buffer_bytes_limit = 32 * 1024 * 1024;

private:
  AsyncFileManager* async_file_manager_;
  const std::string storage_buffer_path_;
  const StreamConfig request_;
  const StreamConfig response_;
};

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
