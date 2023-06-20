#include "source/extensions/filters/http/file_system_buffer/filter_config.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "source/extensions/filters/http/file_system_buffer/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

namespace {

class BufferBehaviorStreamWhenPossible : public BufferBehavior {
public:
  static const BufferBehaviorStreamWhenPossible instance;
};
const BufferBehaviorStreamWhenPossible BufferBehaviorStreamWhenPossible::instance{};

class BufferBehaviorBypass : public BufferBehavior {
public:
  static const BufferBehaviorBypass instance;
  bool bypass() const override { return true; }
};
const BufferBehaviorBypass BufferBehaviorBypass::instance{};

class BufferBehaviorInjectContentLengthIfNecessary : public BufferBehavior {
public:
  static const BufferBehaviorInjectContentLengthIfNecessary instance;
  bool injectContentLength() const override { return true; }
};
const BufferBehaviorInjectContentLengthIfNecessary
    BufferBehaviorInjectContentLengthIfNecessary::instance{};

class BufferBehaviorFullyBufferAndAlwaysInjectContentLength : public BufferBehavior {
public:
  static const BufferBehaviorFullyBufferAndAlwaysInjectContentLength instance;
  bool injectContentLength() const override { return true; }
  bool replaceContentLength() const override { return true; }
  bool alwaysFullyBuffer() const override { return true; }
};
const BufferBehaviorFullyBufferAndAlwaysInjectContentLength
    BufferBehaviorFullyBufferAndAlwaysInjectContentLength::instance{};

class BufferBehaviorFullyBuffer : public BufferBehavior {
public:
  static const BufferBehaviorFullyBuffer instance;
  bool alwaysFullyBuffer() const override { return true; }
};
const BufferBehaviorFullyBuffer BufferBehaviorFullyBuffer::instance{};

} // namespace

// Not in the private namespace to facilitate testing of the NOT_SET path.
const BufferBehavior& selectBufferBehavior(const ProtoBufferBehavior& behavior) {
  switch (behavior.behavior_case()) {
  case ProtoBufferBehavior::kStreamWhenPossible:
    return BufferBehaviorStreamWhenPossible::instance;
  case ProtoBufferBehavior::kBypass:
    return BufferBehaviorBypass::instance;
  case ProtoBufferBehavior::kInjectContentLengthIfNecessary:
    return BufferBehaviorInjectContentLengthIfNecessary::instance;
  case ProtoBufferBehavior::kFullyBufferAndAlwaysInjectContentLength:
    return BufferBehaviorFullyBufferAndAlwaysInjectContentLength::instance;
  case ProtoBufferBehavior::kFullyBuffer:
    return BufferBehaviorFullyBuffer::instance;
  case ProtoBufferBehavior::BEHAVIOR_NOT_SET:
    // This should be impossible with the validate rule, but if it somehow happens we throw
    // an exception.
    break;
  }
  throw EnvoyException("invalid BufferBehavior in FileSystemBufferFilterConfig");
}

namespace {

size_t getMemoryBufferBytesLimit(const StreamConfigVector& configs) {
  for (const ProtoStreamConfig& cfg : configs) {
    if (cfg.has_memory_buffer_bytes_limit()) {
      return cfg.memory_buffer_bytes_limit().value();
    }
  }
  return FileSystemBufferFilterMergedConfig::default_memory_buffer_bytes_limit;
}

size_t getStorageBufferBytesLimit(const StreamConfigVector& configs) {
  for (const ProtoStreamConfig& cfg : configs) {
    if (cfg.has_storage_buffer_bytes_limit()) {
      return cfg.storage_buffer_bytes_limit().value();
    }
  }
  return FileSystemBufferFilterMergedConfig::default_storage_buffer_bytes_limit;
}

size_t getStorageBufferQueueHighWatermarkBytes(const StreamConfigVector& configs) {
  for (const ProtoStreamConfig& cfg : configs) {
    if (cfg.has_storage_buffer_queue_high_watermark_bytes()) {
      return cfg.storage_buffer_queue_high_watermark_bytes().value();
    }
  }
  return std::min(getMemoryBufferBytesLimit(configs), getStorageBufferBytesLimit(configs));
}

absl::string_view getStorageBufferPath(const FilterConfigVector& configs) {
  for (const FileSystemBufferFilterConfig& cfg : configs) {
    if (cfg.proto().has_storage_buffer_path()) {
      return cfg.proto().storage_buffer_path().value();
    }
  }
  auto env_tmpdir = std::getenv("TMPDIR");
  return env_tmpdir ? env_tmpdir : "/tmp";
}

AsyncFileManager* getAsyncFileManager(const FilterConfigVector& configs) {
  for (const FileSystemBufferFilterConfig& cfg : configs) {
    if (cfg.asyncFileManager()) {
      return cfg.asyncFileManager().get();
    }
  }
  return nullptr;
}

const BufferBehavior& getBufferBehavior(const StreamConfigVector& configs) {
  for (const ProtoStreamConfig& cfg : configs) {
    if (cfg.has_behavior()) {
      return selectBufferBehavior(cfg.behavior());
    }
  }
  return BufferBehaviorStreamWhenPossible::instance;
}

const FileSystemBufferFilterMergedConfig::StreamConfig
getBufferRequestConfig(const FilterConfigVector& configs, bool has_file_manager) {
  StreamConfigVector request_configs;
  for (const FileSystemBufferFilterConfig& cfg : configs) {
    if (cfg.proto().has_request()) {
      request_configs.emplace_back(cfg.proto().request());
    }
  }
  return FileSystemBufferFilterMergedConfig::StreamConfig(request_configs, has_file_manager);
}

const FileSystemBufferFilterMergedConfig::StreamConfig
getBufferResponseConfig(const FilterConfigVector& configs, bool has_file_manager) {
  StreamConfigVector response_configs;
  for (const FileSystemBufferFilterConfig& cfg : configs) {
    if (cfg.proto().has_response()) {
      response_configs.emplace_back(cfg.proto().response());
    }
  }
  return FileSystemBufferFilterMergedConfig::StreamConfig(response_configs, has_file_manager);
}

} // namespace

FileSystemBufferFilterMergedConfig::StreamConfig::StreamConfig(const StreamConfigVector& configs,
                                                               bool has_file_manager)
    : memory_buffer_bytes_limit_(getMemoryBufferBytesLimit(configs)),
      storage_buffer_bytes_limit_(has_file_manager ? getStorageBufferBytesLimit(configs) : 0),
      storage_buffer_queue_high_watermark_bytes_(getStorageBufferQueueHighWatermarkBytes(configs)),
      behavior_(getBufferBehavior(configs)) {}

FileSystemBufferFilterConfig::FileSystemBufferFilterConfig(
    std::shared_ptr<AsyncFileManagerFactory> factory,
    std::shared_ptr<AsyncFileManager> async_file_manager,
    const ProtoFileSystemBufferFilterConfig& config)
    : factory_(std::move(factory)), async_file_manager_(async_file_manager), config_(config) {
  if (!config_.storage_buffer_path().value().empty()) {
    struct stat s;
    if (stat(config_.storage_buffer_path().value().c_str(), &s) != 0 ||
        (s.st_mode & S_IFDIR) == 0) {
      throw EnvoyException(
          absl::StrCat("FileSystemBufferFilterConfig: configured storage_buffer_path '",
                       config_.storage_buffer_path().value(), "' is not a directory"));
    }
  }
}

FileSystemBufferFilterMergedConfig::FileSystemBufferFilterMergedConfig(
    const FilterConfigVector& configs)
    : async_file_manager_(getAsyncFileManager(configs)),
      storage_buffer_path_(getStorageBufferPath(configs)),
      request_(getBufferRequestConfig(configs, async_file_manager_ != nullptr)),
      response_(getBufferResponseConfig(configs, async_file_manager_ != nullptr)) {}

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
