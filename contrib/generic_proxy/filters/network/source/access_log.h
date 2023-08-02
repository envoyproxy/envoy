#pragma once

#include "contrib/generic_proxy/filters/network/source/file_access_log_base.h"
#include "contrib/generic_proxy/filters/network/source/formatter_impl_base.h"
#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

struct GenericProxyFormatterContext {
  const Request* request_{};
  const Response* response_{};

  static constexpr absl::string_view category() { return "generic_proxy"; }
};

using GenericProxyFormatter = Formatter<GenericProxyFormatterContext>;
using GenericProxyFormatterProvider = FormatterProvider<GenericProxyFormatterContext>;
using GenericProxyFormatterProviderPtr = std::unique_ptr<GenericProxyFormatterProvider>;
using GenericProxyCommandParser = CommandParser<GenericProxyFormatterContext>;
using GenericProxyCommandParserFactory = CommandParserFactory<GenericProxyFormatterContext>;
using GenericProxyBuiltInCommandParsers = BuiltInCommandParsers<GenericProxyFormatterContext>;

using GenericProxyAccessLogInstance = AccessLogInstance<GenericProxyFormatterContext>;
using GenericProxyAccessLogInstanceSharedPtr = std::shared_ptr<GenericProxyAccessLogInstance>;
using GenericProxyAccessLogInstanceFactory = AccessLogInstanceFactory<GenericProxyFormatterContext>;
using GenericProxyFileAccessLog = FileAccessLog<GenericProxyFormatterContext>;
using GenericProxyFileAccessLogFactory = FileAccessLogFactory<GenericProxyFormatterContext>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
