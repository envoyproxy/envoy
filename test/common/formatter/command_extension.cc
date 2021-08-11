#include "test/common/formatter/command_extension.h"

#include "source/common/protobuf/utility.h"

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

FormatterProviderPtr TestCommandParser::parse(const std::string& token, size_t, size_t) const {
  if (absl::StartsWith(token, "COMMAND_EXTENSION")) {
    return std::make_unique<TestFormatter>();
  }

  return nullptr;
}

CommandParserPtr
TestCommandFactory::createCommandParserFromProto(const Protobuf::Message& message) {
  // Cast the config message to the actual type to test that it was constructed properly.
  [[maybe_unused]] const auto config = dynamic_cast<const ProtobufWkt::StringValue&>(message);
  return std::make_unique<TestCommandParser>();
}

std::string TestCommandFactory::configType() { return "google.protobuf.StringValue"; }

ProtobufTypes::MessagePtr TestCommandFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::StringValue>();
}

std::string TestCommandFactory::name() const { return "envoy.formatter.TestFormatter"; }

absl::optional<std::string> AdditionalFormatter::format(const Http::RequestHeaderMap&,
                                                        const Http::ResponseHeaderMap&,
                                                        const Http::ResponseTrailerMap&,
                                                        const StreamInfo::StreamInfo&,
                                                        absl::string_view) const {
  return "AdditionalFormatter";
}

ProtobufWkt::Value AdditionalFormatter::formatValue(const Http::RequestHeaderMap&,
                                                    const Http::ResponseHeaderMap&,
                                                    const Http::ResponseTrailerMap&,
                                                    const StreamInfo::StreamInfo&,
                                                    absl::string_view) const {
  return ValueUtil::stringValue("");
}

FormatterProviderPtr AdditionalCommandParser::parse(const std::string& token, size_t,
                                                    size_t) const {
  if (absl::StartsWith(token, "ADDITIONAL_EXTENSION")) {
    return std::make_unique<AdditionalFormatter>();
  }

  return nullptr;
}

CommandParserPtr
AdditionalCommandFactory::createCommandParserFromProto(const Protobuf::Message& message) {
  // Cast the config message to the actual type to test that it was constructed properly.
  [[maybe_unused]] const auto config = dynamic_cast<const ProtobufWkt::UInt32Value&>(message);
  return std::make_unique<AdditionalCommandParser>();
}

std::string AdditionalCommandFactory::configType() { return "google.protobuf.UInt32Value"; }

ProtobufTypes::MessagePtr AdditionalCommandFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::UInt32Value>();
}

std::string AdditionalCommandFactory::name() const { return "envoy.formatter.AdditionalFormatter"; }

CommandParserPtr
FailCommandFactory::createCommandParserFromProto(const Protobuf::Message& message) {
  // Cast the config message to the actual type to test that it was constructed properly.
  [[maybe_unused]] const auto config = dynamic_cast<const ProtobufWkt::UInt64Value&>(message);
  return nullptr;
}

std::string FailCommandFactory::configType() { return "google.protobuf.UInt64Value"; }

ProtobufTypes::MessagePtr FailCommandFactory::createEmptyConfigProto() {
  return std::make_unique<ProtobufWkt::UInt64Value>();
}

std::string FailCommandFactory::name() const { return "envoy.formatter.FailFormatter"; }

} // namespace Formatter
} // namespace Envoy
