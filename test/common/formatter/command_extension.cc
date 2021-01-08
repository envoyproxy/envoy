#include "test/common/formatter/command_extension.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Formatter {

absl::optional<std::string> TestFormatter::format(const Http::RequestHeaderMap&,
                                                  const Http::ResponseHeaderMap&,
                                                  const Http::ResponseTrailerMap&,
                                                  const StreamInfo::StreamInfo&,
                                                  absl::string_view) const {
  return "TestFormatter";
}

ProtobufWkt::Value TestFormatter::formatValue(const Http::RequestHeaderMap&,
                                              const Http::ResponseHeaderMap&,
                                              const Http::ResponseTrailerMap&,
                                              const StreamInfo::StreamInfo&,
                                              absl::string_view) const {
  return ValueUtil::stringValue("");
}

FormatterProviderPtr TestCommandParser::parse(const std::string& token, size_t, int) const {
  if (absl::StartsWith(token, "COMMAND_EXTENSION")) {
    return std::make_unique<TestFormatter>();
  }

  return nullptr;
}

CommandParserPtr TestCommandFactory::createCommandParserFromProto(const Protobuf::Message&) {
  return std::make_unique<TestCommandParser>();
}

std::string TestCommandFactory::configType() { return "google.protobuf.StringValue"; }

ProtobufTypes::MessagePtr TestCommandFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::StringValue>();
}

std::string TestCommandFactory::name() const { return "envoy.formatter.TestFormatter"; }

} // namespace Formatter
} // namespace Envoy
