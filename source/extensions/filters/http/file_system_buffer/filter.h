#include <memory>
#include <string>

#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/file_system_buffer/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

// TODO(ravenblack): this is a placeholder filter to be filled in a subsequent PR.
class FileSystemBufferFilter : public Http::PassThroughFilter,
                               public Logger::Loggable<Logger::Id::http2> {
public:
  explicit FileSystemBufferFilter(std::shared_ptr<FileSystemBufferFilterConfig> base_config);

  static const std::string& filterName();

private:
  const std::shared_ptr<FileSystemBufferFilterConfig> base_config_;

  friend class FileSystemBufferFilterConfigTest; // to inspect base_config_
};

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
