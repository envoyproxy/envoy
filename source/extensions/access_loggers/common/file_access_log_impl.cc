#include "source/extensions/access_loggers/common/file_access_log_impl.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {

FileAccessLog::FileAccessLog(const Filesystem::FilePathAndType& access_log_file_info,
                             AccessLog::FilterPtr&& filter, Formatter::FormatterPtr&& formatter,
                             AccessLog::AccessLogManager& log_manager)
    : ImplBase(std::move(filter)), formatter_(std::move(formatter)) {
  auto file_or_error = log_manager.createAccessLog(access_log_file_info);
  THROW_IF_NOT_OK_REF(file_or_error.status());
  log_file_ = file_or_error.value();
}

void FileAccessLog::emitLog(const Formatter::HttpFormatterContext& context,
                            const StreamInfo::StreamInfo& stream_info) {
  log_file_->write(formatter_->formatWithContext(context, stream_info));
}

} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
