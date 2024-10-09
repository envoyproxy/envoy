#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/common/matchers.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/extensions/filters/http/oauth2/v3/oauth.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/matchers.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/utility.h"
#include "source/common/secret/secret_provider_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/oauth2/oauth.h"
#include "source/extensions/filters/http/oauth2/oauth_client.h"

#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

class OAuth2Client;

// Helper class used to fetch secrets (usually from SDS).
class SecretReader {
public:
  virtual ~SecretReader() = default;
  virtual const std::string& clientSecret() const PURE;
  virtual const std::string& tokenSecret() const PURE;
};

class SDSSecretReader : public SecretReader {
public:
  SDSSecretReader(Secret::GenericSecretConfigProviderSharedPtr&& client_secret_provider,
                  Secret::GenericSecretConfigProviderSharedPtr&& token_secret_provider,
                  ThreadLocal::SlotAllocator& tls, Api::Api& api)
      : client_secret_(std::move(client_secret_provider), tls, api),
        token_secret_(std::move(token_secret_provider), tls, api) {}
  const std::string& clientSecret() const override { return client_secret_.secret(); }
  const std::string& tokenSecret() const override { return token_secret_.secret(); }

private:
  Secret::ThreadLocalGenericSecretProvider client_secret_;
  Secret::ThreadLocalGenericSecretProvider token_secret_;
};

/**
 * All stats for the OAuth filter. @see stats_macros.h
 */
#define ALL_OAUTH_FILTER_STATS(COUNTER)                                                            \
  COUNTER(oauth_unauthorized_rq)                                                                   \
  COUNTER(oauth_failure)                                                                           \
  COUNTER(oauth_passthrough)                                                                       \
  COUNTER(oauth_success)                                                                           \
  COUNTER(oauth_refreshtoken_success)                                                              \
  COUNTER(oauth_refreshtoken_failure)

/**
 * Wrapper struct filter stats. @see stats_macros.h
 */
struct FilterStats {
  ALL_OAUTH_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Helper structure to hold custom cookie names.
 */
struct CookieNames {
  CookieNames(const envoy::extensions::filters::http::oauth2::v3::OAuth2Credentials::CookieNames&
                  cookie_names)
      : CookieNames(cookie_names.bearer_token(), cookie_names.oauth_hmac(),
                    cookie_names.oauth_expires(), cookie_names.id_token(),
                    cookie_names.refresh_token(), cookie_names.oauth_nonce()) {}

  CookieNames(const std::string& bearer_token, const std::string& oauth_hmac,
              const std::string& oauth_expires, const std::string& id_token,
              const std::string& refresh_token, const std::string& oauth_nonce)
      : bearer_token_(bearer_token.empty() ? BearerToken : bearer_token),
        oauth_hmac_(oauth_hmac.empty() ? OauthHMAC : oauth_hmac),
        oauth_expires_(oauth_expires.empty() ? OauthExpires : oauth_expires),
        id_token_(id_token.empty() ? IdToken : id_token),
        refresh_token_(refresh_token.empty() ? RefreshToken : refresh_token),
        oauth_nonce_(oauth_nonce.empty() ? OauthNonce : oauth_nonce) {}

  const std::string bearer_token_;
  const std::string oauth_hmac_;
  const std::string oauth_expires_;
  const std::string id_token_;
  const std::string refresh_token_;
  const std::string oauth_nonce_;

  static constexpr absl::string_view OauthExpires = "OauthExpires";
  static constexpr absl::string_view BearerToken = "BearerToken";
  static constexpr absl::string_view OauthHMAC = "OauthHMAC";
  static constexpr absl::string_view OauthNonce = "OauthNonce";
  static constexpr absl::string_view IdToken = "IdToken";
  static constexpr absl::string_view RefreshToken = "RefreshToken";
};

/**
 * This class encapsulates all data needed for the filter to operate so that we don't pass around
 * raw protobufs and other arbitrary data.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::oauth2::v3::OAuth2Config& proto_config,
               Server::Configuration::CommonFactoryContext& context,
               std::shared_ptr<SecretReader> secret_reader, Stats::Scope& scope,
               const std::string& stats_prefix);
  const std::string& clusterName() const { return oauth_token_endpoint_.cluster(); }
  const std::string& clientId() const { return client_id_; }
  bool forwardBearerToken() const { return forward_bearer_token_; }
  bool preserveAuthorizationHeader() const { return preserve_authorization_header_; }
  const std::vector<Http::HeaderUtility::HeaderData>& passThroughMatchers() const {
    return pass_through_header_matchers_;
  }
  const std::vector<Http::HeaderUtility::HeaderData>& denyRedirectMatchers() const {
    return deny_redirect_header_matchers_;
  }
  const HttpUri& oauthTokenEndpoint() const { return oauth_token_endpoint_; }
  const Http::Utility::Url& authorizationEndpointUrl() const { return authorization_endpoint_url_; }
  const Http::Utility::QueryParamsMulti& authorizationQueryParams() const {
    return authorization_query_params_;
  }
  const std::string& redirectUri() const { return redirect_uri_; }
  const Matchers::PathMatcher& redirectPathMatcher() const { return redirect_matcher_; }
  const Matchers::PathMatcher& signoutPath() const { return signout_path_; }
  std::string clientSecret() const { return secret_reader_->clientSecret(); }
  std::string tokenSecret() const { return secret_reader_->tokenSecret(); }
  FilterStats& stats() { return stats_; }
  const std::string& encodedResourceQueryParams() const { return encoded_resource_query_params_; }
  const CookieNames& cookieNames() const { return cookie_names_; }
  const std::string& cookieDomain() const { return cookie_domain_; }
  const AuthType& authType() const { return auth_type_; }
  bool useRefreshToken() const { return use_refresh_token_; }
  std::chrono::seconds defaultExpiresIn() const { return default_expires_in_; }
  std::chrono::seconds defaultRefreshTokenExpiresIn() const {
    return default_refresh_token_expires_in_;
  }
  bool disableIdTokenSetCookie() const { return disable_id_token_set_cookie_; }
  bool disableAccessTokenSetCookie() const { return disable_access_token_set_cookie_; }
  bool disableRefreshTokenSetCookie() const { return disable_refresh_token_set_cookie_; }
  const OptRef<const RouteRetryPolicy> retryPolicy() const {
    if (!retry_policy_.has_value()) {
      return absl::nullopt;
    }
    return makeOptRef(retry_policy_.value());
  }

private:
  static FilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  const HttpUri oauth_token_endpoint_;
  // Owns the data exposed by authorization_endpoint_url_.
  const std::string authorization_endpoint_;
  Http::Utility::Url authorization_endpoint_url_;
  const Http::Utility::QueryParamsMulti authorization_query_params_;
  const std::string client_id_;
  const std::string redirect_uri_;
  const Matchers::PathMatcher redirect_matcher_;
  const Matchers::PathMatcher signout_path_;
  std::shared_ptr<SecretReader> secret_reader_;
  FilterStats stats_;
  const std::string encoded_auth_scopes_;
  const std::string encoded_resource_query_params_;
  const std::vector<Http::HeaderUtility::HeaderData> pass_through_header_matchers_;
  const std::vector<Http::HeaderUtility::HeaderData> deny_redirect_header_matchers_;
  const CookieNames cookie_names_;
  const std::string cookie_domain_;
  const AuthType auth_type_;
  const std::chrono::seconds default_expires_in_;
  const std::chrono::seconds default_refresh_token_expires_in_;
  const bool forward_bearer_token_ : 1;
  const bool preserve_authorization_header_ : 1;
  const bool use_refresh_token_ : 1;
  const bool disable_id_token_set_cookie_ : 1;
  const bool disable_access_token_set_cookie_ : 1;
  const bool disable_refresh_token_set_cookie_ : 1;
  absl::optional<RouteRetryPolicy> retry_policy_;
};

using FilterConfigSharedPtr = std::shared_ptr<FilterConfig>;

/**
 * An OAuth cookie validator:
 * 1. extracts cookies from a request
 * 2. HMAC/encodes the values
 * 3. Compares the result to the cookie HMAC
 * 4. Checks that the `expires` value is valid relative to current time
 *
 * Required components:
 * - header map
 * - secret
 */
class CookieValidator {
public:
  virtual ~CookieValidator() = default;
  virtual const std::string& token() const PURE;
  virtual const std::string& refreshToken() const PURE;
  virtual void setParams(const Http::RequestHeaderMap& headers, const std::string& secret) PURE;
  virtual bool isValid() const PURE;
  virtual bool canUpdateTokenByRefreshToken() const PURE;
};

class OAuth2CookieValidator : public CookieValidator {
public:
  explicit OAuth2CookieValidator(TimeSource& time_source, const CookieNames& cookie_names,
                                 const std::string& cookie_domain)
      : time_source_(time_source), cookie_names_(cookie_names), cookie_domain_(cookie_domain) {}

  const std::string& token() const override { return token_; }
  const std::string& refreshToken() const override { return refresh_token_; }

  void setParams(const Http::RequestHeaderMap& headers, const std::string& secret) override;
  bool isValid() const override;
  bool hmacIsValid() const;
  bool timestampIsValid() const;
  bool canUpdateTokenByRefreshToken() const override;

private:
  std::string token_;
  std::string id_token_;
  std::string refresh_token_;
  std::string expires_;
  std::string hmac_;
  std::vector<uint8_t> secret_;
  absl::string_view host_;
  TimeSource& time_source_;
  const CookieNames cookie_names_;
  const std::string cookie_domain_;
};

struct CallbackValidationResult {
  bool is_valid_;
  std::string auth_code_;
  std::string original_request_url_;
};

/**
 * The filter is the primary entry point for the OAuth workflow. Its responsibilities are to
 * receive incoming requests and decide at what state of the OAuth workflow they are in. Logic
 * beyond that is broken into component classes.
 */
class OAuth2Filter : public Http::PassThroughFilter,
                     FilterCallbacks,
                     Logger::Loggable<Logger::Id::oauth2> {
public:
  OAuth2Filter(FilterConfigSharedPtr config, std::unique_ptr<OAuth2Client>&& oauth_client,
               TimeSource& time_source);

  // Http::PassThroughFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers, bool) override;

  // FilterCallbacks
  void onGetAccessTokenSuccess(const std::string& access_code, const std::string& id_token,
                               const std::string& refresh_token,
                               std::chrono::seconds expires_in) override;

  void onRefreshAccessTokenSuccess(const std::string& access_code, const std::string& id_token,
                                   const std::string& refresh_token,
                                   std::chrono::seconds expires_in) override;

  void onRefreshAccessTokenFailure() override;

  // a catch-all function used for request failures. we don't retry, as a user can simply refresh
  // the page in the case of a network blip.
  void sendUnauthorizedResponse() override;

  void finishGetAccessTokenFlow();
  void finishRefreshAccessTokenFlow();
  void updateTokens(const std::string& access_token, const std::string& id_token,
                    const std::string& refresh_token, std::chrono::seconds expires_in);
  bool validateNonce(const Http::RequestHeaderMap& headers, const std::string& nonce);

private:
  friend class OAuth2Test;

  std::shared_ptr<CookieValidator> validator_;

  // wrap up some of these in a UserData struct or something...
  std::string auth_code_;
  std::string access_token_; // TODO - see if we can avoid this being a member variable
  std::string id_token_;
  std::string refresh_token_;
  std::string expires_in_;
  std::string expires_refresh_token_in_;
  std::string expires_id_token_in_;
  std::string new_expires_;
  absl::string_view host_;
  std::string original_request_url_;
  Http::RequestHeaderMap* request_headers_{nullptr};
  bool was_refresh_token_flow_{false};

  std::unique_ptr<OAuth2Client> oauth_client_;
  FilterConfigSharedPtr config_;
  TimeSource& time_source_;

  // Determines whether or not the current request can skip the entire OAuth flow (HMAC is valid,
  // connection is mTLS, etc.)
  bool canSkipOAuth(Http::RequestHeaderMap& headers) const;
  bool canRedirectToOAuthServer(Http::RequestHeaderMap& headers) const;
  void redirectToOAuthServer(Http::RequestHeaderMap& headers) const;

  Http::FilterHeadersStatus signOutUser(const Http::RequestHeaderMap& headers);

  std::string getEncodedToken() const;
  std::string getExpiresTimeForRefreshToken(const std::string& refresh_token,
                                            const std::chrono::seconds& expires_in) const;
  std::string getExpiresTimeForIdToken(const std::string& id_token,
                                       const std::chrono::seconds& expires_in) const;
  void addResponseCookies(Http::ResponseHeaderMap& headers, const std::string& encoded_token) const;
  const std::string& bearerPrefix() const;
  CallbackValidationResult validateOAuthCallback(const Http::RequestHeaderMap& headers,
                                                 const absl::string_view path_str);
};

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
