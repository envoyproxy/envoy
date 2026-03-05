#include "source/extensions/formatter/datasource/config.h"

#include "envoy/extensions/formatter/datasource/v3/datasource.pb.h"
#include "envoy/extensions/formatter/datasource/v3/datasource.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/factory_context.h"

#include "source/common/config/datasource.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

namespace {

constexpr absl::string_view DataSourceCommand = "DATASOURCE";

/**
 * FormatterProvider that returns the current value of a named DataSource.
 * For file-based DataSources, the value is automatically updated when the file changes.
 */
class DataSourceFormatterProvider : public Envoy::Formatter::FormatterProvider {
public:
  DataSourceFormatterProvider(Config::DataSource::DataSourceProviderSharedPtr<std::string> provider)
      : provider_(std::move(provider)) {}

  absl::optional<std::string> format(const Envoy::Formatter::Context&,
                                     const StreamInfo::StreamInfo&) const override {
    const auto data = provider_->data();
    if (!data) {
      return absl::nullopt;
    }
    return *data;
  }

  Protobuf::Value formatValue(const Envoy::Formatter::Context& context,
                              const StreamInfo::StreamInfo& stream_info) const override {
    Protobuf::Value val;
    const auto opt = format(context, stream_info);
    if (opt.has_value()) {
      val.set_string_value(*opt);
    }
    return val;
  }

private:
  Config::DataSource::DataSourceProviderSharedPtr<std::string> provider_;
};

/**
 * CommandParser that handles the %DATASOURCE(name)% command.
 * Looks up the named DataSource provider from the map built at construction time.
 */
class DataSourceCommandParser : public Envoy::Formatter::CommandParser {
public:
  using ProviderMap =
      absl::flat_hash_map<std::string,
                          Config::DataSource::DataSourceProviderSharedPtr<std::string>>;

  DataSourceCommandParser(ProviderMap providers) : providers_(std::move(providers)) {}

  Envoy::Formatter::FormatterProviderPtr parse(absl::string_view command,
                                               absl::string_view subcommand,
                                               absl::optional<size_t>) const override {
    if (command != DataSourceCommand) {
      return nullptr;
    }
    const auto it = providers_.find(subcommand);
    if (it == providers_.end()) {
      return nullptr;
    }
    return std::make_unique<DataSourceFormatterProvider>(it->second);
  }

private:
  ProviderMap providers_;
};

} // namespace

Envoy::Formatter::CommandParserPtr DataSourceFormatterFactory::createCommandParserFromProto(
    const Protobuf::Message& config, Server::Configuration::GenericFactoryContext& context) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::formatter::datasource::v3::DataSource&>(
      config, context.messageValidationVisitor());

  auto& server_context = context.serverFactoryContext();

  // Enable modify_watch so that file-based DataSources are automatically re-read
  // when the file changes on disk, propagating the update to all worker threads.
  const Config::DataSource::ProviderOptions options{.allow_empty = true, .modify_watch = true};

  DataSourceCommandParser::ProviderMap providers;
  for (const auto& [name, source] : typed_config.datasources()) {
    auto provider = THROW_OR_RETURN_VALUE(
        Config::DataSource::DataSourceProvider<std::string>::create(
            source, server_context.mainThreadDispatcher(), server_context.threadLocal(),
            server_context.api(),
            [](absl::string_view data) -> absl::StatusOr<std::shared_ptr<std::string>> {
              return std::make_shared<std::string>(data);
            },
            options),
        Config::DataSource::DataSourceProviderPtr<std::string>);

    providers.emplace(
        name, Config::DataSource::DataSourceProviderSharedPtr<std::string>(std::move(provider)));
  }

  return std::make_unique<DataSourceCommandParser>(std::move(providers));
}

ProtobufTypes::MessagePtr DataSourceFormatterFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::formatter::datasource::v3::DataSource>();
}

std::string DataSourceFormatterFactory::name() const { return "envoy.formatter.datasource"; }

REGISTER_FACTORY(DataSourceFormatterFactory, Envoy::Formatter::CommandParserFactory);

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
