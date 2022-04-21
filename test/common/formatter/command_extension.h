#pragma once

#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Formatter {

class TestFormatter : public FormatterProvider {
public:
  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;
};

class TestCommandParser : public CommandParser {
public:
  FormatterProviderPtr parse(const std::string& command, const std::string& subcommand,
                             absl::optional<size_t>& max_length) const override;
};

class TestCommandFactory : public CommandParserFactory {
public:
  CommandParserPtr createCommandParserFromProto(const Protobuf::Message&) override;
  std::string configType() override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
};

class AdditionalFormatter : public FormatterProvider {
public:
  // FormatterProvider
  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;
};

class AdditionalCommandParser : public CommandParser {
public:
  FormatterProviderPtr parse(const std::string& command, const std::string& subcommand,
                             absl::optional<size_t>& max_length) const override;
};

class AdditionalCommandFactory : public CommandParserFactory {
public:
  CommandParserPtr createCommandParserFromProto(const Protobuf::Message&) override;
  std::string configType() override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
};

class FailCommandFactory : public CommandParserFactory {
public:
  CommandParserPtr createCommandParserFromProto(const Protobuf::Message&) override;
  std::string configType() override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
};

} // namespace Formatter
} // namespace Envoy
