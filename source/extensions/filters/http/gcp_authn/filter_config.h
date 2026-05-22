#pragma once

#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/matchers.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/gcp_authn/gcp_authn_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {

class FilterConfig : public Logger::Loggable<Logger::Id::filter> {
public:
  FilterConfig(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& proto_config,
      Server::Configuration::FactoryContext& context);

  const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& protoConfig() const {
    return proto_config_;
  }

  const envoy::extensions::filters::http::gcp_authn::v3::TokenHeader& token_header() const {
    return proto_config_.token_header();
  }

  const envoy::extensions::filters::http::gcp_authn::v3::TokenCacheConfig& cache_config() const {
    return proto_config_.cache_config();
  }

  bool has_retry_policy() const { return proto_config_.has_retry_policy(); }

  const envoy::config::core::v3::RetryPolicy& retry_policy() const {
    return proto_config_.retry_policy();
  }

  std::string clientCertFingerprint() const;

private:
  void updateFingerprint();

  const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig proto_config_;
  Server::Configuration::FactoryContext& context_;

  Secret::TlsCertificateConfigProviderSharedPtr tls_cert_provider_;
  Envoy::Common::CallbackHandlePtr tls_cert_update_handle_;
  std::vector<Matchers::StringMatcherImpl> san_matchers_;

  struct ThreadLocalFingerprint : public ThreadLocal::ThreadLocalObject {
    explicit ThreadLocalFingerprint(const std::string& fingerprint) : fingerprint_(fingerprint) {}
    const std::string fingerprint_;
  };
  Envoy::ThreadLocal::TypedSlot<ThreadLocalFingerprint> tls_slot_;
};

using FilterConfigSharedPtr = std::shared_ptr<const FilterConfig>;

class GcpAuthnFilterFactory
    : public Common::FactoryBase<
          envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig>,
      public Logger::Loggable<Logger::Id::filter> {
public:
  GcpAuthnFilterFactory() : FactoryBase(std::string(FilterName)) {}

  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
