#pragma once

#include "envoy/secret/secret_provider.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

#include "envoy/extensions/filters/http/credentials/v3alpha/injector.pb.h"

#include "source/extensions/filters/http/credentials/oauth_client.h"
#include "source/extensions/filters/http/credentials/secret_reader.h"
#include "source/extensions/filters/http/credentials/source.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

class Oauth2ClientCredentialsGrantCredentialRequestImpl : public CredentialSource::Request, public FilterCallbacks {
public:
  Oauth2ClientCredentialsGrantCredentialRequestImpl(
    CredentialSource::Callbacks& callbacks, std::unique_ptr<OAuth2Client>&& oauth_client,
    absl::string_view client_id, absl::string_view client_secret) :
    callbacks_(callbacks), oauth_client_(std::move(oauth_client)), client_id_(client_id), client_secret_(client_secret) {
    oauth_client_->setCallbacks(*this);
    oauth_client_->asyncGetAccessToken(client_id_, client_secret_);
  }

  // FilterCallbacks
  void onGetAccessTokenSuccess(const std::string& access_code,
                               const std::string&,
                               std::chrono::seconds) override {
    auto const token = absl::StrCat("Bearer ", access_code);;

    callbacks_.onSuccess(token);
  }

  void sendUnauthorizedResponse() override {
    callbacks_.onFailure();
  }

  // CredentialSource::Request
  void cancel() override {
  }
private:
  CredentialSource::Callbacks& callbacks_;
  std::unique_ptr<OAuth2Client> oauth_client_;
  const std::string client_id_;
  const std::string client_secret_;
};

class ThreadLocalOauth2ClientCredentialsGrantCredentialSource : public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalOauth2ClientCredentialsGrantCredentialSource(absl::string_view client_id, absl::string_view client_secret) :
    client_id_(client_id), client_secret_(client_secret) {}

  const std::string& client_id() const { return client_id_; };
  const std::string& client_secret() const { return client_secret_; };
private:
  std::string client_id_;
  std::string client_secret_;
};
using ThreadLocalOauth2ClientCredentialsGrantCredentialSourceSharedPtr = std::shared_ptr<ThreadLocalOauth2ClientCredentialsGrantCredentialSource>;

/**
 * Source of OAuth2 Client Credentials Grant credential.
 */
class Oauth2ClientCredentialsGrantCredentialSource : public CredentialSource, public SecretReader::Callbacks {
public:
  virtual ~Oauth2ClientCredentialsGrantCredentialSource() = default;

  Oauth2ClientCredentialsGrantCredentialSource(Upstream::ClusterManager& cluster_manager,
                                               ThreadLocal::SlotAllocator& tls,
                                               Api::Api& api,
                                               const envoy::extensions::filters::http::credentials::v3alpha::OAuth2Credential& proto_config,
                                               Secret::GenericSecretConfigProviderSharedPtr client_id_secret,
                                               Secret::GenericSecretConfigProviderSharedPtr client_secret_secret);

  const ThreadLocalOauth2ClientCredentialsGrantCredentialSource& threadLocal() const {
    return tls_->getTyped<ThreadLocalOauth2ClientCredentialsGrantCredentialSource>();
  }

  // CredentialSource
  CredentialSource::RequestPtr requestCredential(CredentialSource::Callbacks& callbacks) override {
    const auto& tls = threadLocal();
    std::unique_ptr<OAuth2Client> oauth_client =
      std::make_unique<OAuth2ClientImpl>(cluster_manager_, proto_config_.token_endpoint());
    return std::make_unique<Oauth2ClientCredentialsGrantCredentialRequestImpl>(callbacks, std::move(oauth_client), tls.client_id(), tls.client_secret());
  }

  // SecretReader::Callbacks
  void onSecretUpdate() override;

private:
  Upstream::ClusterManager& cluster_manager_;
  ThreadLocal::SlotPtr tls_;

  const envoy::extensions::filters::http::credentials::v3alpha::OAuth2Credential proto_config_;

  const SecretReaderPtr client_id_reader_;
  const SecretReaderPtr client_secret_reader_;
};

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
