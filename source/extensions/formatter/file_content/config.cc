#include "source/extensions/formatter/file_content/config.h"

#include "envoy/extensions/formatter/file_content/v3/file_content.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/utility.h"
#include "source/common/config/datasource.h"
#include "source/common/formatter/substitution_format_utility.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

namespace {

constexpr absl::string_view FileContentCommand = "FILE_CONTENT";

/**
 * FormatterProvider backed by a DataSourceProvider with file watching.
 * The DataSourceProvider automatically re-reads the file when it changes on disk.
 */
class FileContentFormatterProvider : public Envoy::Formatter::FormatterProvider {
public:
  FileContentFormatterProvider(
      Config::DataSource::DataSourceProviderSharedPtr<std::string> provider)
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
 * CommandParser that handles the %FILE_CONTENT(/path/to/file)% or
 * %FILE_CONTENT(/path/to/file:/path/to/watch)% command.
 * Creates a DataSourceProvider with file watching for each parsed file path.
 * When a watch directory is specified, changes in that directory trigger a re-read of the file.
 */
class FileContentCommandParser : public Envoy::Formatter::CommandParser {
public:
  explicit FileContentCommandParser(Server::Configuration::ServerFactoryContext& server_context)
      : server_context_(server_context) {}

  Envoy::Formatter::FormatterProviderPtr parse(absl::string_view command,
                                               absl::string_view subcommand,
                                               absl::optional<size_t> max_length) const override {
    if (command != FileContentCommand) {
      return nullptr;
    }

    // This formatter creates thread locals which can only happen on the main thread.
    ASSERT_IS_MAIN_OR_TEST_THREAD();

    envoy::config::core::v3::DataSource source;
    // Split subcommand on ':' to extract filename and optional watch directory.
    // Format: /path/to/file or /path/to/file:/path/to/watch
    const auto parts = StringUtil::splitToken(subcommand, ":", /*keep_empty_string=*/false);
    if (parts.empty() || parts.size() > 2) {
      throw EnvoyException(fmt::format(
          "FILE_CONTENT: expected format 'path' or 'path:watch_directory', got '{}'", subcommand));
    }
    source.set_filename(std::string(parts[0]));
    if (parts.size() == 2) {
      source.mutable_watched_directory()->set_path(std::string(parts[1]));
    }
    const Config::DataSource::ProviderOptions options{.allow_empty = true, .modify_watch = true};

    auto provider = THROW_OR_RETURN_VALUE(
        Config::DataSource::DataSourceProvider<std::string>::create(
            source, server_context_.mainThreadDispatcher(), server_context_.threadLocal(),
            server_context_.api(),
            [max_length](absl::string_view data) -> absl::StatusOr<std::shared_ptr<std::string>> {
              auto result = std::make_shared<std::string>(data);
              Envoy::Formatter::SubstitutionFormatUtils::truncate(*result, max_length);
              return result;
            },
            options),
        Config::DataSource::DataSourceProviderPtr<std::string>);

    return std::make_unique<FileContentFormatterProvider>(
        Config::DataSource::DataSourceProviderSharedPtr<std::string>(std::move(provider)));
  }

private:
  Server::Configuration::ServerFactoryContext& server_context_;
};

} // namespace

Envoy::Formatter::CommandParserPtr FileContentFormatterFactory::createCommandParserFromProto(
    const Protobuf::Message&, Server::Configuration::GenericFactoryContext& context) {
  return std::make_unique<FileContentCommandParser>(context.serverFactoryContext());
}

ProtobufTypes::MessagePtr FileContentFormatterFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::formatter::file_content::v3::FileContent>();
}

std::string FileContentFormatterFactory::name() const { return "envoy.formatter.file_content"; }

REGISTER_FACTORY(FileContentFormatterFactory, Envoy::Formatter::CommandParserFactory);

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
