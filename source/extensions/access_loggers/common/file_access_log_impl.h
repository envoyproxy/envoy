#pragma once

#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/access_loggers/common/access_log_base.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

/**
 * Access log Instance that writes logs to a file.
 */
class FileAccessLog : public Common::ImplBase {
public:
  FileAccessLog(const Filesystem::FilePathAndType& access_log_file_info,
                AccessLog::FilterPtr&& filter, Formatter::FormatterPtr&& formatter,
                AccessLog::AccessLogManager& log_manager);

private:
  // Common::ImplBase
  void emitLog(const Formatter::HttpFormatterContext& context,
               const StreamInfo::StreamInfo& stream_info) override;

  AccessLog::AccessLogFileSharedPtr log_file_;
  Formatter::FormatterPtr formatter_;
};

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
