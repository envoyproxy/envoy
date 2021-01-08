#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "common/formatter/substitution_formatter.h"

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
  TestCommandParser() = default;
  FormatterProviderPtr parse(const std::string& token, size_t, int) const override;
};

class TestCommandFactory : public CommandParserFactory {
public:
  CommandParserPtr createCommandParserFromProto(const Protobuf::Message&) override;
  std::string configType() override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override;
};

} // namespace Formatter
} // namespace Envoy
