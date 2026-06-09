#include "source/extensions/formatter/all_headers/config.h"

#include "envoy/extensions/formatter/all_headers/v3/all_headers.pb.h"
#include "envoy/extensions/formatter/all_headers/v3/all_headers.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/formatter/all_headers/all_headers.h"

#include "absl/strings/ascii.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

::Envoy::Formatter::CommandParserPtr AllHeadersFormatterFactory::createCommandParserFromProto(
    const Protobuf::Message& message, Server::Configuration::GenericFactoryContext& context) {
  const auto& config = MessageUtil::downcastAndValidate<
      const envoy::extensions::formatter::all_headers::v3::AllHeaders&>(
      message, context.messageValidationVisitor());

  absl::flat_hash_set<std::string> exclude_headers;
  for (const auto& header : config.exclude_headers()) {
    exclude_headers.insert(absl::AsciiStrToLower(header));
  }

  return std::make_unique<AllHeadersCommandParser>(config.max_value_bytes(),
                                                   std::move(exclude_headers));
}

ProtobufTypes::MessagePtr AllHeadersFormatterFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::formatter::all_headers::v3::AllHeaders>();
}

std::string AllHeadersFormatterFactory::name() const { return "envoy.formatter.all_headers"; }

REGISTER_FACTORY(AllHeadersFormatterFactory, ::Envoy::Formatter::CommandParserFactory);

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
