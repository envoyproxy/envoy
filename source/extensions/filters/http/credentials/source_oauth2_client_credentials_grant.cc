#include "source/extensions/filters/http/credentials/source_oauth2_client_credentials_grant.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

Oauth2ClientCredentialsGrantCredentialSource::Oauth2ClientCredentialsGrantCredentialSource(
    Upstream::ClusterManager& cluster_manager, ThreadLocal::SlotAllocator& tls, Api::Api& api,
    const envoy::extensions::filters::http::credential_injector::v3::OAuth2Credential& proto_config,
    std::string client_id, Secret::GenericSecretConfigProviderSharedPtr client_secret_secret)
    : cluster_manager_(cluster_manager), tls_(tls.allocateSlot()), proto_config_(proto_config),
      client_id_(client_id),
      client_secret_reader_(std::make_unique<SDSSecretReader>(client_secret_secret, api, *this)) {
  ThreadLocalOauth2ClientCredentialsGrantCredentialSourceSharedPtr empty(
      new ThreadLocalOauth2ClientCredentialsGrantCredentialSource(client_id, ""));
  tls_->set(
      [empty](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return empty; });

  onSecretUpdate();
}

void Oauth2ClientCredentialsGrantCredentialSource::onSecretUpdate() {
  if (!client_secret_reader_) {
    return;
  }
  const auto& client_secret = StringUtil::trim(client_secret_reader_->value());
  if (client_secret.empty()) {
    return;
  }

  ThreadLocalOauth2ClientCredentialsGrantCredentialSourceSharedPtr value(
      new ThreadLocalOauth2ClientCredentialsGrantCredentialSource(client_id_, client_secret));
  tls_->set(
      [value](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr { return value; });
}

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
