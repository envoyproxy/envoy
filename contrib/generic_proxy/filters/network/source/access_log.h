#pragma once

#include "contrib/generic_proxy/filters/network/source/file_access_log.h"
#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

struct FormatterContext {
  const Request* request_{};
  const Response* response_{};

  static constexpr absl::string_view category() { return "generic_proxy"; }
};

// Formatter for generic proxy.
using Formatter = Formatter::FormatterBase<FormatterContext>;
using FormatterProvider = Envoy::Formatter::FormatterProviderBase<FormatterContext>;
using FormatterProviderPtr = std::unique_ptr<FormatterProvider>;
using CommandParser = Envoy::Formatter::CommandParserBase<FormatterContext>;
using CommandParserFactory = Envoy::Formatter::CommandParserFactoryBase<FormatterContext>;
using BuiltInCommandParsers = Envoy::Formatter::BuiltInCommandParsersBase<FormatterContext>;

// Access log for generic proxy.
using AccessLogFilter = AccessLog::FilterBase<FormatterContext>;
using AccessLogFilterPtr = std::unique_ptr<AccessLogFilter>;
using AccessLogFilterFactory = AccessLog::ExtensionFilterFactoryBase<FormatterContext>;
using AccessLogInstance = AccessLog::InstanceBase<FormatterContext>;
using AccessLogInstanceSharedPtr = std::shared_ptr<AccessLogInstance>;
using AccessLogInstanceFactory = AccessLog::AccessLogInstanceFactoryBase<FormatterContext>;

// File access log for generic proxy.
using FileAccessLog = FileAccessLogBase<FormatterContext>;
using FileAccessLogFactory = FileAccessLogFactoryBase<FormatterContext>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
