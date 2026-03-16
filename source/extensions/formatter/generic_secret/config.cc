#include "source/extensions/formatter/generic_secret/config.h"

#include "envoy/extensions/formatter/generic_secret/v3/generic_secret.pb.h"
#include "envoy/extensions/formatter/generic_secret/v3/generic_secret.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"

#include "source/common/common/logger.h"
#include "source/common/formatter/substitution_format_utility.h"
#include "source/common/secret/secret_provider_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

namespace {

constexpr absl::string_view SecretCommand = "SECRET";

/**
 * FormatterProvider that returns the current value of a named generic secret.
 */
class GenericSecretFormatterProvider : public Envoy::Formatter::FormatterProvider {
public:
  GenericSecretFormatterProvider(
      std::shared_ptr<Secret::ThreadLocalGenericSecretProvider> secret_provider,
      absl::optional<size_t> max_length)
      : secret_provider_(std::move(secret_provider)), max_length_(max_length) {}

  absl::optional<std::string> format(const Envoy::Formatter::Context&,
                                     const StreamInfo::StreamInfo&) const override {
    const std::string& value = secret_provider_->secret();
    if (value.empty()) {
      return absl::nullopt;
    }
    std::string result = value;
    Envoy::Formatter::SubstitutionFormatUtils::truncate(result, max_length_);
    return result;
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
  std::shared_ptr<Secret::ThreadLocalGenericSecretProvider> secret_provider_;
  const absl::optional<size_t> max_length_;
};

/**
 * CommandParser that handles the %SECRET(name)% command.
 * Looks up the named secret from the map built at construction time.
 */
class GenericSecretCommandParser : public Envoy::Formatter::CommandParser {
public:
  using ProviderMap =
      absl::flat_hash_map<std::string, std::shared_ptr<Secret::ThreadLocalGenericSecretProvider>>;

  explicit GenericSecretCommandParser(ProviderMap providers) : providers_(std::move(providers)) {}

  Envoy::Formatter::FormatterProviderPtr parse(absl::string_view command,
                                               absl::string_view subcommand,
                                               absl::optional<size_t> max_length) const override {
    if (command != SecretCommand) {
      return nullptr;
    }
    const auto it = providers_.find(subcommand);
    if (it == providers_.end()) {
      throw EnvoyException(fmt::format(
          "envoy.formatter.generic_secret: secret '{}' is not configured in secret_configs",
          subcommand));
    }
    return std::make_unique<GenericSecretFormatterProvider>(it->second, max_length);
  }

private:
  ProviderMap providers_;
};

} // namespace

Envoy::Formatter::CommandParserPtr GenericSecretFormatterFactory::createCommandParserFromProto(
    const Protobuf::Message& config, Server::Configuration::GenericFactoryContext& context) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::formatter::generic_secret::v3::GenericSecret&>(
      config, context.messageValidationVisitor());

  // This formatter creates thread locals which can only happen on the main thread.
  ASSERT_IS_MAIN_OR_TEST_THREAD();

  auto& server_context = context.serverFactoryContext();

  GenericSecretCommandParser::ProviderMap providers;
  for (const auto& entry : typed_config.secret_configs()) {
    const std::string& name = entry.first;
    const auto& secret_config = entry.second;
    Secret::GenericSecretConfigProviderSharedPtr provider;
    if (secret_config.has_sds_config()) {
      provider = server_context.secretManager().findOrCreateGenericSecretProvider(
          secret_config.sds_config(), secret_config.name(), server_context, context.initManager());
    } else {
      provider =
          server_context.secretManager().findStaticGenericSecretProvider(secret_config.name());
      if (provider == nullptr) {
        throw EnvoyException(
            fmt::format("envoy.formatter.generic_secret: secret '{}' not found in static "
                        "bootstrap resources",
                        secret_config.name()));
      }
    }

    auto tls_provider = THROW_OR_RETURN_VALUE(
        Secret::ThreadLocalGenericSecretProvider::create(
            std::move(provider), server_context.threadLocal(), server_context.api()),
        std::unique_ptr<Secret::ThreadLocalGenericSecretProvider>);

    providers.emplace(
        name, std::shared_ptr<Secret::ThreadLocalGenericSecretProvider>(std::move(tls_provider)));
  }

  return std::make_unique<GenericSecretCommandParser>(std::move(providers));
}

ProtobufTypes::MessagePtr GenericSecretFormatterFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::formatter::generic_secret::v3::GenericSecret>();
}

std::string GenericSecretFormatterFactory::name() const { return "envoy.formatter.generic_secret"; }

REGISTER_FACTORY(GenericSecretFormatterFactory, Envoy::Formatter::CommandParserFactory);

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
