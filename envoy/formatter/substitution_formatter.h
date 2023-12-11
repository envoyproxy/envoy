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

/**
 * Implemented by each custom CommandParser and registered via Registry::registerFactory()
 * or the convenience class RegisterFactory.
 * Specialization of CommandParserFactoryBase for HTTP and backwards compatibliity.
 */
template <> class CommandParserFactoryBase<HttpFormatterContext> : public Config::TypedFactory {
public:
  ~CommandParserFactoryBase() override = default;

  /**
   * Creates a particular CommandParser implementation.
   *
   * @param config supplies the configuration for the command parser.
   * @param context supplies the factory context.
   * @return CommandParserPtr the CommandParser which will be used in
   * SubstitutionFormatParser::parse() when evaluating an access log format string.
   */
  virtual CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message& config,
                               Server::Configuration::GenericFactoryContext& context) PURE;

  std::string category() const override { return "envoy.formatter"; }
};

using CommandParserFactory = CommandParserFactoryBase<HttpFormatterContext>;

} // namespace Formatter
} // namespace Envoy
