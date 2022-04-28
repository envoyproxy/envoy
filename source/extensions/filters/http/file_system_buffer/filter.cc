#include "source/extensions/filters/http/file_system_buffer/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

FileSystemBufferFilter::FileSystemBufferFilter(
    std::shared_ptr<FileSystemBufferFilterConfig> base_config)
    : base_config_(base_config) {}

const std::string& FileSystemBufferFilter::filterName() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy.filters.http.file_system_buffer");
}

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
