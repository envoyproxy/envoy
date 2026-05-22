#include "source/extensions/filters/http/gcp_authn/filter_config.h"

#include <memory>

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/secret/secret_manager.h"

#include "source/common/http/utility.h"
#include "source/extensions/filters/http/gcp_authn/crypto_utils.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;

Http::FilterFactoryCb GcpAuthnFilterFactory::createFilterFactoryFromProtoTyped(
    const GcpAuthnFilterConfig& config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<TokenCache> token_cache;
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.cache_config(), cache_size, 0) > 0) {
    token_cache = std::make_shared<TokenCache>(config.cache_config(), context);
  }
  // config.retry_policy has an invalid case that could not be validated by the
  // proto validation annotation. It has to be validated by the code.
  if (config.has_retry_policy()) {
    THROW_IF_NOT_OK(Http::Utility::validateCoreRetryPolicy(config.retry_policy()));
  }

  FilterConfigSharedPtr filter_config = std::make_shared<FilterConfig>(config, context);

  return [config, stats_prefix, &context, token_cache = std::move(token_cache),
          filter_config =
              std::move(filter_config)](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    absl::optional<std::string> fingerprint = absl::nullopt;
    if (filter_config->protoConfig().has_token_binding_config()) {
      fingerprint = filter_config->clientCertFingerprint();
    }
    callbacks.addStreamFilter(std::make_shared<GcpAuthnFilter>(
        filter_config->protoConfig(), fingerprint, context, stats_prefix,
        (token_cache != nullptr) ? &token_cache->tls.get()->cache() : nullptr));
  };
}

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GcpAuthnFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& proto_config,
    Server::Configuration::FactoryContext& context)
    : proto_config_(proto_config), context_(context),
      tls_slot_(context.serverFactoryContext().threadLocal()) {

  tls_slot_.set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalFingerprint>(""); });

  if (proto_config_.has_token_binding_config()) {
    const auto& binding_config = proto_config_.token_binding_config();

    for (const auto& matcher : binding_config.client_certificate_san_matchers()) {
      san_matchers_.emplace_back(matcher, context_.serverFactoryContext());
    }

    const auto& client_cert = binding_config.client_certificate();
    if (client_cert.has_sds_config()) {
      tls_cert_provider_ =
          context.serverFactoryContext().secretManager().findOrCreateTlsCertificateProvider(
              client_cert.sds_config(), client_cert.name(), context.serverFactoryContext(),
              context.initManager(), true);
    } else {
      tls_cert_provider_ =
          context.serverFactoryContext().secretManager().findStaticTlsCertificateProvider(
              client_cert.name());
    }

    if (tls_cert_provider_ != nullptr) {
      tls_cert_update_handle_ = tls_cert_provider_->addUpdateCallback([this]() {
        updateFingerprint();
        return absl::OkStatus();
      });
      updateFingerprint();
    } else {
      ENVOY_LOG_MISC(warn, "TlsCertificateConfigProvider not found for {}", client_cert.name());
    }
  }
}

std::string FilterConfig::clientCertFingerprint() const {
  auto slot = tls_slot_.get();
  if (slot.has_value()) {
    return slot->fingerprint_;
  }
  return "";
}

void FilterConfig::updateFingerprint() {
  std::string fingerprint = "";
  if (tls_cert_provider_ != nullptr) {
    auto fingerprint_or_error = getBase64EncodedCertificateFingerprint(
        tls_cert_provider_, san_matchers_, context_.serverFactoryContext().api());
    if (fingerprint_or_error.ok()) {
      fingerprint = fingerprint_or_error.value();
    } else {
      ENVOY_LOG_MISC(warn, "Failed to get certificate fingerprint: {}",
                     fingerprint_or_error.status().message());
    }
  }

  tls_slot_.set([fingerprint](Envoy::Event::Dispatcher&) {
    return std::make_shared<ThreadLocalFingerprint>(fingerprint);
  });
}

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
