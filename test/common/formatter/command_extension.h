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
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;

  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;
};

class TestCommandParser : public CommandParser {
public:
  FormatterProviderPtr parse(absl::string_view command, absl::string_view subcommand,
                             absl::optional<size_t> max_length) const override;
};

class TestCommandFactory : public CommandParserFactory {
public:
  CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message&,
                               Server::Configuration::GenericFactoryContext&) override;
  std::set<std::string> configTypes() override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
};

class AdditionalFormatter : public FormatterProvider {
public:
  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const HttpFormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;

  ProtobufWkt::Value
  formatValueWithContext(const HttpFormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;
};

class AdditionalCommandParser : public CommandParser {
public:
  FormatterProviderPtr parse(absl::string_view command, absl::string_view subcommand,
                             absl::optional<size_t> max_length) const override;
};

class AdditionalCommandFactory : public CommandParserFactory {
public:
  CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message&,
                               Server::Configuration::GenericFactoryContext&) override;
  std::set<std::string> configTypes() override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
};

class FailCommandFactory : public CommandParserFactory {
public:
  CommandParserPtr
  createCommandParserFromProto(const Protobuf::Message&,
                               Server::Configuration::GenericFactoryContext&) override;
  std::set<std::string> configTypes() override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
};

} // namespace Formatter
} // namespace Envoy
