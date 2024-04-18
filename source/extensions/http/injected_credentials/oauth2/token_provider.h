#pragma once

#include "envoy/extensions/http/injected_credentials/oauth2/v3/oauth2.pb.h"
#include "envoy/extensions/http/injected_credentials/oauth2/v3/oauth2.pb.validate.h"

#include "source/extensions/http/injected_credentials/common/credential.h"
#include "source/extensions/http/injected_credentials/common/secret_reader.h"
#include "source/extensions/http/injected_credentials/oauth2/oauth_client.h"
#include <string>

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

using envoy::extensions::http::injected_credentials::oauth2::v3::OAuth2;

class ThreadLocalOauth2ClientCredentialsToken : public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalOauth2ClientCredentialsToken(absl::string_view token) : token_(token) {}

  const std::string& token() const { return token_; };

private:
  std::string token_;
};

using ThreadLocalOauth2ClientCredentialsTokenSharedPtr =
    std::shared_ptr<ThreadLocalOauth2ClientCredentialsToken>;
class TokenReader {
public:
  virtual ~TokenReader() = default;
  virtual const std::string& token() const PURE;
};

using TokenReaderConstSharedPtr = std::shared_ptr<const TokenReader>;

class TokenProvider : public TokenReader,
                      public FilterCallbacks,
                      public Logger::Loggable<Logger::Id::credential_injector> {
public:
  TokenProvider(Common::SecretReaderConstSharedPtr secret_reader, ThreadLocal::SlotAllocator& tls,
                Upstream::ClusterManager& cm, const OAuth2& proto_config,
                Event::Dispatcher& dispatcher);
  void asyncGetAccessToken();

  const ThreadLocalOauth2ClientCredentialsToken& threadLocal() const {
    return tls_->getTyped<ThreadLocalOauth2ClientCredentialsToken>();
  }

  // TokenReader
  const std::string& token() const override;

  // FilterCallbacks
  void onGetAccessTokenSuccess(const std::string& access_code, std::chrono::seconds) override;

private:
  std::string token_;
  const Common::SecretReaderConstSharedPtr secret_reader_;
  ThreadLocal::SlotPtr tls_;
  std::unique_ptr<OAuth2Client> oauth2_client_;
  std::string client_id_;
  Event::Dispatcher* dispatcher_;
  Event::TimerPtr timer_;
};

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
