#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/matchers.h"
#include "envoy/config/core/v3/http_uri.pb.h"
#include "envoy/extensions/filters/http/oauth2/v3alpha/oauth.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/matchers.h"
#include "common/config/datasource.h"
#include "common/formatter/substitution_formatter.h"
#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"
#include "common/http/rest_api_fetcher.h"
#include "common/http/utility.h"

#include "extensions/filters/http/common/pass_through_filter.h"
#include "extensions/filters/http/oauth2/oauth.h"
#include "extensions/filters/http/oauth2/oauth_client.h"

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
  SDSSecretReader(Secret::GenericSecretConfigProviderSharedPtr client_secret_provider,
                  Secret::GenericSecretConfigProviderSharedPtr token_secret_provider, Api::Api& api)
      : api_(api), client_secret_provider_(std::move(client_secret_provider)),
        token_secret_provider_(std::move(token_secret_provider)) {
    readAndWatchSecret(client_secret_, *client_secret_provider_);
    readAndWatchSecret(token_secret_, *token_secret_provider_);
  }

  const std::string& clientSecret() const override { return client_secret_; }

  const std::string& tokenSecret() const override { return token_secret_; }

private:
  void readAndWatchSecret(std::string& value,
                          Secret::GenericSecretConfigProvider& secret_provider) {
    const auto* secret = secret_provider.secret();
    if (secret != nullptr) {
      value = Config::DataSource::read(secret->secret(), true, api_);
    }

    secret_provider.addUpdateCallback([&secret_provider, this, &value]() {
      const auto* secret = secret_provider.secret();
      if (secret != nullptr) {
        value = Config::DataSource::read(secret->secret(), true, api_);
      }
    });
  }
  std::string client_secret_;
  std::string token_secret_;
  Api::Api& api_;

  Secret::GenericSecretConfigProviderSharedPtr client_secret_provider_;
  Secret::GenericSecretConfigProviderSharedPtr token_secret_provider_;
};

/**
 * All stats for the OAuth filter. @see stats_macros.h
 */
#define ALL_OAUTH_FILTER_STATS(COUNTER)                                                            \
  COUNTER(oauth_unauthorized_rq)                                                                   \
  COUNTER(oauth_failure)                                                                           \
  COUNTER(oauth_success)

/**
 * Wrapper struct filter stats. @see stats_macros.h
 */
struct FilterStats {
  ALL_OAUTH_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * This class encapsulates all data needed for the filter to operate so that we don't pass around
 * raw protobufs and other arbitrary data.
 */
class FilterConfig {
public:
  FilterConfig(const envoy::extensions::filters::http::oauth2::v3alpha::OAuth2Config& proto_config,
               Upstream::ClusterManager& cluster_manager,
               std::shared_ptr<SecretReader> secret_reader, Stats::Scope& scope,
               const std::string& stats_prefix);
  const std::string& clusterName() const { return oauth_token_endpoint_.cluster(); }
  const std::string& clientId() const { return client_id_; }
  bool forwardBearerToken() const { return forward_bearer_token_; }
  const std::vector<Http::HeaderUtility::HeaderData>& passThroughMatchers() const {
    return pass_through_header_matchers_;
  }

  const envoy::config::core::v3::HttpUri& oauthTokenEndpoint() const {
    return oauth_token_endpoint_;
  }
  const std::string& authorizationEndpoint() const { return authorization_endpoint_; }
  const std::string& redirectUri() const { return redirect_uri_; }
  const Matchers::PathMatcher& redirectPathMatcher() const { return redirect_matcher_; }
  const Matchers::PathMatcher& signoutPath() const { return signout_path_; }
  std::string clientSecret() const { return secret_reader_->clientSecret(); }
  std::string tokenSecret() const { return secret_reader_->tokenSecret(); }
  FilterStats& stats() { return stats_; }
  const std::string& encodedAuthScopes() const { return encoded_auth_scopes_; }
  const std::string& encodedResources() const { return encoded_resources_; }

private:
  static FilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  const envoy::config::core::v3::HttpUri oauth_token_endpoint_;
  const std::string authorization_endpoint_;
  const std::string client_id_;
  const std::string redirect_uri_;
  const Matchers::PathMatcher redirect_matcher_;
  const Matchers::PathMatcher signout_path_;
  std::shared_ptr<SecretReader> secret_reader_;
  FilterStats stats_;
  const std::string encoded_auth_scopes_;
  const std::string encoded_resources_;
  const bool forward_bearer_token_ : 1;
  const std::vector<Http::HeaderUtility::HeaderData> pass_through_header_matchers_;
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
  virtual void setParams(const Http::RequestHeaderMap& headers, const std::string& secret) PURE;
  virtual bool isValid() const PURE;
};

class OAuth2CookieValidator : public CookieValidator {
public:
  explicit OAuth2CookieValidator(TimeSource& time_source) : time_source_(time_source) {}

  const std::string& token() const override { return token_; }
  void setParams(const Http::RequestHeaderMap& headers, const std::string& secret) override;
  bool isValid() const override;
  bool hmacIsValid() const;
  bool timestampIsValid() const;

private:
  std::string token_;
  std::string expires_;
  std::string hmac_;
  std::vector<uint8_t> secret_;
  absl::string_view host_;
  TimeSource& time_source_;
};

/**
 * The filter is the primary entry point for the OAuth workflow. Its responsibilities are to
 * receive incoming requests and decide at what state of the OAuth workflow they are in. Logic
 * beyond that is broken into component classes.
 */
class OAuth2Filter : public Http::PassThroughDecoderFilter, public FilterCallbacks {
public:
  OAuth2Filter(FilterConfigSharedPtr config, std::unique_ptr<OAuth2Client>&& oauth_client,
               TimeSource& time_source);

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

  // FilterCallbacks
  void onGetAccessTokenSuccess(const std::string& access_code,
                               std::chrono::seconds expires_in) override;
  // a catch-all function used for request failures. we don't retry, as a user can simply refresh
  // the page in the case of a network blip.
  void sendUnauthorizedResponse() override;

  void finishFlow();

private:
  friend class OAuth2Test;

  std::shared_ptr<CookieValidator> validator_;

  // wrap up some of these in a UserData struct or something...
  std::string auth_code_;
  std::string access_token_; // TODO - see if we can avoid this being a member variable
  std::string new_expires_;
  absl::string_view host_;
  std::string state_;
  bool found_bearer_token_{false};
  Http::RequestHeaderMap* request_headers_{nullptr};

  std::unique_ptr<OAuth2Client> oauth_client_;
  FilterConfigSharedPtr config_;
  TimeSource& time_source_;

  // Determines whether or not the current request can skip the entire OAuth flow (HMAC is valid,
  // connection is mTLS, etc.)
  bool canSkipOAuth(Http::RequestHeaderMap& headers) const;

  Http::FilterHeadersStatus signOutUser(const Http::RequestHeaderMap& headers);

  const std::string& bearerPrefix() const;
  std::string extractAccessToken(const Http::RequestHeaderMap& headers) const;
};

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
