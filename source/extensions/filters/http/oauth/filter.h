#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

#include "common/common/assert.h"
#include "common/config/datasource.h"
#include "common/http/rest_api_fetcher.h"

#include "envoy/upstream/cluster_manager.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/http/common/pass_through_filter.h"

#include "envoy/extensions/filters/http/oauth/v3/oauth.pb.h"
#include "extensions/filters/http/oauth/oauth.h"
#include "extensions/filters/http/oauth/oauth_client.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth {

class OAuth2Client;

// Helper class used to fetch secrets (usually from SDS).
class SecretReader {
public:
  virtual ~SecretReader() = default;
  virtual std::string clientSecret() const PURE;
  virtual std::string tokenSecret() const PURE;
};

class SDSSecretReader : public SecretReader {
public:
  SDSSecretReader(Secret::GenericSecretConfigProviderSharedPtr client_secret_provider,
                  Secret::GenericSecretConfigProviderSharedPtr token_secret_provider, Api::Api& api)
      : client_secret_provider_(client_secret_provider),
        token_secret_provider_(token_secret_provider), api_(api) {}

  std::string clientSecret() const override {
    if (!client_secret_provider_) {
      return "";
    }
    const auto* secret = client_secret_provider_->secret();
    if (!secret) {
      return "";
    }
    return Config::DataSource::read(secret->secret(), true, api_);
  }

  std::string tokenSecret() const override {
    if (!token_secret_provider_) {
      return "";
    }
    const auto* secret = token_secret_provider_->secret();
    if (!secret) {
      return "";
    }
    return Config::DataSource::read(secret->secret(), true, api_);
  }

private:
  Secret::GenericSecretConfigProviderSharedPtr client_secret_provider_;
  Secret::GenericSecretConfigProviderSharedPtr token_secret_provider_;
  Api::Api& api_;
};

/**
 * This class encapsulates all data needed for the filter to operate so that we don't pass around
 * raw protobufs and other arbitrary data.
 */
class OAuth2FilterConfig {
public:
  OAuth2FilterConfig(const envoy::extensions::filters::http::oauth::v3::OAuth2& proto_config,
                     Upstream::ClusterManager& cluster_manager,
                     std::shared_ptr<SecretReader> secret_reader);
  const std::string& clusterName() const { return cluster_name_; }
  const std::string& clientId() const { return client_id_; }
  bool forwardBearerToken() const { return forward_bearer_token_; }
  bool useXEnvoyOauth() const { return use_x_envoy_oauth_; }
  bool passThroughOptionsMethod() const { return pass_through_options_method_; }
  const std::vector<std::string>& whitelistedPaths() const { return whitelisted_paths_; }
  const std::string& oauthServerHostname() const { return oauth_server_hostname_; }
  const std::string& callbackPath() const { return callback_path_; }
  const std::string& signoutPath() const { return signout_path_; }
  std::string clientSecret() const { return secret_reader_->clientSecret(); }
  std::string tokenSecret() const { return secret_reader_->tokenSecret(); }

private:
  const std::string cluster_name_;
  const std::string client_id_;
  const std::string oauth_server_hostname_;
  const std::string callback_path_;
  const std::string signout_path_;
  std::vector<std::string> whitelisted_paths_{"/robots.txt", "/favicon.ico", "/manifest.json"};
  const bool forward_bearer_token_ : 1;
  const bool use_x_envoy_oauth_ : 1;
  const bool pass_through_options_method_ : 1;
  std::shared_ptr<SecretReader> secret_reader_;
};

using OAuth2FilterConfigSharedPtr = std::shared_ptr<OAuth2FilterConfig>;

/**
 * An OAuth cookie validator:
 * 1. extracts cookies from a request
 * 2. HMAC/encodes the values
 * 3. Compares the result to the cookie HMAC
 * 4. Checks that the `expires` value is valid relative to current time
 *
 * Required components:
 * - headermap
 * - secret
 */
class CookieValidator {
public:
  virtual ~CookieValidator() = default;
  virtual const std::string& username() const PURE;
  virtual const std::string& token() const PURE;
  virtual void setParams(const Http::RequestHeaderMap& headers, const std::string& secret) PURE;
  virtual bool isValid() const PURE;
};

class OAuth2CookieValidator : public CookieValidator {
public:
  OAuth2CookieValidator() = default;
  ~OAuth2CookieValidator() override = default;
  const std::string& username() const override { return username_; }
  const std::string& token() const override { return token_; }
  void setParams(const Http::RequestHeaderMap& headers, const std::string& secret) override;
  bool isValid() const override;
  bool hmacIsValid() const;
  bool timestampIsValid() const;

private:
  std::string username_;
  std::string token_;
  std::string expires_;
  std::string hmac_;
  std::vector<uint8_t> secret_;
  absl::string_view host_;
};

/**
 * The filter is the primary entrypoint for the OAuth workflow. Its responsibilities are to
 * receive incoming requests and decide at what state of the OAuth workflow they are in. Logic
 * beyond that is broken into component classes.
 */
class OAuth2Filter : public Http::PassThroughDecoderFilter, public OAuth2FilterCallbacks {
public:
  OAuth2Filter(OAuth2FilterConfigSharedPtr config, std::unique_ptr<OAuth2Client>&& oauth_client,
               Stats::Scope& scope);
  ~OAuth2Filter() override = default;

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  void onGetAccessTokenSuccess(const std::string& access_code,
                               const std::string& expires_in) override;
  void onGetIdentitySuccess(const std::string& username) override;
  // a catch-all function used for request failures. we don't retry, as a user can simply refresh
  // the page in the case of a network blip.
  void sendUnauthorizedResponse() override;

  // Set the x-forwarded-user after successfully validating the client cookies.
  static void setXForwardedOauthHeaders(Http::RequestHeaderMap& headers, const std::string& token,
                                        const std::string& username);

private:
  friend class OAuth2Test;

  using RequirementsMap = absl::flat_hash_map<absl::string_view, absl::string_view>;

  std::shared_ptr<CookieValidator> validator_;

  // wrap up some of these in a UserData struct or something...
  std::string auth_code_{};
  std::string access_token_{}; // TODO - see if we can avoid this being a member variable
  std::string username_;
  std::string new_expires_;
  absl::string_view host_;
  std::string user_agent_;
  std::string state_{};
  bool found_bearer_token_{false};
  Http::RequestHeaderMap* request_headers_{nullptr};

  std::unique_ptr<OAuth2Client> oauth_client_;
  OAuth2FilterConfigSharedPtr config_;
  Stats::Scope& scope_;

  const RequirementsMap& escapedReplacements() const {
    CONSTRUCT_ON_FIRST_USE(RequirementsMap,
                           {{"/", "%2F"}, {":", "%3A"}, {"?", "%3F"}, {"&", "%26"}, {"=", "%3D"}});
  }

  const RequirementsMap& unescapedReplacements() const {
    CONSTRUCT_ON_FIRST_USE(RequirementsMap,
                           {{"%2F", "/"}, {"%3A", ":"}, {"%3F", "?"}, {"%26", "&"}, {"%3D", "="}});
  }

  // Sanitize the x-forwarded-user headers before the main filter logic.
  static void sanitizeXForwardedOauthHeaders(Http::RequestHeaderMap& headers);

  // Determines whether or not the current request can skip the entire OAuth flow (HMAC is valid,
  // connection is mTLS, etc.)
  bool canSkipOAuth(Http::RequestHeaderMap& headers) const;

  Http::FilterHeadersStatus signOutUser(const Http::RequestHeaderMap& headers);

  const std::string& bearerPrefix() const;
  std::string extractAccessToken(const Http::RequestHeaderMap& headers,
                                 const std::string& parameter_name) const;
};

} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
