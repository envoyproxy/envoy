#pragma once

#include "envoy/formatter/http_formatter_context.h"
#include "envoy/formatter/substitution_formatter_base.h"
#include "envoy/http/header_map.h"

namespace Envoy {
namespace Formatter {

using Formatter = FormatterBase<HttpFormatterContext>;
using FormatterPtr = std::unique_ptr<Formatter>;
using FormatterConstSharedPtr = std::shared_ptr<const Formatter>;

using FormatterProvider = FormatterProviderBase<HttpFormatterContext>;
using FormatterProviderPtr = std::unique_ptr<FormatterProvider>;

using CommandParser = CommandParserBase<HttpFormatterContext>;
using CommandParserPtr = std::unique_ptr<CommandParser>;
using CommandParserFactory = CommandParserFactoryBase<HttpFormatterContext>;

} // namespace Formatter
} // namespace Envoy
