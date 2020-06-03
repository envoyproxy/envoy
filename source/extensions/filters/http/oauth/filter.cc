#include "extensions/filters/http/oauth/filter.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/common/matchers.h"
#include "common/crypto/utility.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth {

// X-Forwarded Oauth headers
const std::string Token{"token"};
const Http::LowerCaseString& xForwardedUser() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "x-forwarded-user");
}

const std::string& signoutCookieValue() {
  CONSTRUCT_ON_FIRST_USE(std::string,
                         "OauthHMAC=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT");
}

const std::string& signoutBearerTokenValue() {
  CONSTRUCT_ON_FIRST_USE(std::string,
                         "BearerToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT");
}

const std::string& cookieTailFormatString() {
  CONSTRUCT_ON_FIRST_USE(std::string, ";version=1;path=/;Max-Age={};secure");
}

const std::string& cookieTailHttpOnlyFormatString() {
  CONSTRUCT_ON_FIRST_USE(std::string, ";version=1;path=/;Max-Age={};secure;HttpOnly");
}

const std::string& authClusterUriParameters() {
  CONSTRUCT_ON_FIRST_USE(
      std::string,
      "https://{}/oauth/"
      "authorize/?client_id={}&scope=user&response_type=code&redirect_uri={}&state={}");
}

const std::string& unauthorizedBodyMessage() {
  CONSTRUCT_ON_FIRST_USE(std::string, "OAuth flow failed.");
}

const std::string& defaultOauthCallback() { CONSTRUCT_ON_FIRST_USE(std::string, "/_oauth"); }
const std::string& defaultOauthSignout() { CONSTRUCT_ON_FIRST_USE(std::string, "/_signout"); }

const std::string& queryParamsError() { CONSTRUCT_ON_FIRST_USE(std::string, "error"); }
const std::string& queryParamsCode() { CONSTRUCT_ON_FIRST_USE(std::string, "code"); }
const std::string& queryParamsState() { CONSTRUCT_ON_FIRST_USE(std::string, "state"); }

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::oauth::v3::OAuth2Config& proto_config,
    Upstream::ClusterManager& cluster_manager, std::shared_ptr<SecretReader> secret_reader,
    Stats::Scope& scope, const std::string& stats_prefix)
    : cluster_name_(proto_config.cluster()), client_id_(proto_config.credentials().client_id()),
      oauth_server_hostname_(proto_config.hostname()),
      callback_path_(
          PROTOBUF_GET_STRING_OR_DEFAULT(proto_config, callback_path, defaultOauthCallback())),
      signout_path_(
          PROTOBUF_GET_STRING_OR_DEFAULT(proto_config, signout_path, defaultOauthSignout())),
      secret_reader_(secret_reader), stats_(FilterConfig::generateStats(stats_prefix, scope)),
      forward_bearer_token_(proto_config.forward_bearer_token()),
      pass_through_options_method_(proto_config.pass_through_options_method()) {
  if (!cluster_manager.get(cluster_name_)) {
    throw EnvoyException(fmt::format("OAuth2 filter: unknown cluster '{}' in config. Please "
                                     "specify which cluster to direct OAuth requests to.",
                                     cluster_name_));
  }
}

FilterStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  return {ALL_OAUTH_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

void OAuth2CookieValidator::setParams(const Http::RequestHeaderMap& headers,
                                      const std::string& secret) {
  expires_ = Http::Utility::parseCookieValue(headers, "OauthExpires");
  username_ = Http::Utility::parseCookieValue(headers, "OauthUsername");
  token_ = Http::Utility::parseCookieValue(headers, "BearerToken");
  hmac_ = Http::Utility::parseCookieValue(headers, "OauthHMAC");
  host_ = headers.Host()->value().getStringView();

  std::vector<uint8_t> vec(secret.begin(), secret.end());
  secret_ = std::move(vec);
}

bool OAuth2CookieValidator::hmacIsValid() const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const std::string hmac_payload = absl::StrCat(host_, username_, expires_, token_);
  const std::string pre_encoded_hmac =
      Hex::encode(crypto_util.getSha256Hmac(secret_, hmac_payload));
  std::string encoded_hmac;
  absl::Base64Escape(pre_encoded_hmac, &encoded_hmac);

  return encoded_hmac == hmac_;
}

bool OAuth2CookieValidator::timestampIsValid() const {
  std::time_t epoch = std::time(nullptr);
  int expires;
  try {
    expires = std::stoi(expires_);
  } catch (const std::exception&) {
    return false;
  }

  return expires > epoch;
}

bool OAuth2CookieValidator::isValid() const { return hmacIsValid() && timestampIsValid(); }

OAuth2Filter::OAuth2Filter(FilterConfigSharedPtr config,
                           std::unique_ptr<OAuth2Client>&& oauth_client)
    : validator_(std::make_shared<OAuth2CookieValidator>()), oauth_client_(std::move(oauth_client)),
      config_(std::move(config)) {

  oauth_client_->setCallbacks(*this);
}

const std::string& OAuth2Filter::bearerPrefix() const {
  CONSTRUCT_ON_FIRST_USE(std::string, "bearer ");
}

std::string OAuth2Filter::extractAccessToken(const Http::RequestHeaderMap& headers,
                                             const std::string& parameter_name) const {
  ASSERT(headers.Path() != nullptr);

  // Start by looking for a bearer token in the Authorization header.
  const Http::HeaderEntry* authorization = headers.Authorization();
  if (authorization != nullptr) {
    const auto value = StringUtil::trim(authorization->value().getStringView());
    const auto& bearer_prefix = bearerPrefix();
    if (absl::StartsWithIgnoreCase(value, bearer_prefix)) {
      const size_t start = bearer_prefix.length();
      return std::string(StringUtil::ltrim(value.substr(start)));
    }
  }

  // Check for the named query string parameter.
  const auto path = headers.Path()->value().getStringView();
  const auto params = Http::Utility::parseQueryString(path);
  const auto param = params.find(parameter_name);
  if (param != params.end()) {
    return param->second;
  }

  return EMPTY_STRING;
}

/**
 * primary cases:
 * 1) user is signing out
 * 2) /_oauth redirect
 * 3) user is authorized
 * 4) user is unauthorized
 */
Http::FilterHeadersStatus OAuth2Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {

  // The following 2 headers are guaranteed for regular requests. The asserts are helpful when
  // writing test code to not forget these important variables in mock requests
  const Http::HeaderEntry* host_header = headers.Host();
  ASSERT(host_header != nullptr);
  host_ = host_header->value().getStringView();

  const Http::HeaderEntry* path_header = headers.Path();
  ASSERT(path_header != nullptr);
  const absl::string_view path_str = path_header->value().getStringView();

  sanitizeXForwardedOauthHeaders(headers);

  // We should check if this is a sign out request.
  if (path_str == config_->signoutPath()) {
    return signOutUser(headers);
  }

  if (canSkipOAuth(headers)) {
    // Update the path header with the query string parameters after a successful OAuth login.
    // This is necessary if a website requests multiple resources which get redirected to the
    // auth server. A cached login on the authentication server side will set cookies
    // correctly but cause a race condition on future requests that have their location set
    // to the callback path.
    if (absl::StartsWith(path_str, config_->callbackPath())) {
      Http::Utility::QueryParams query_parameters = Http::Utility::parseQueryString(path_str);

      const auto state =
          absl::StrReplaceAll(query_parameters.at(queryParamsState()), unescapedReplacements());
      Http::Utility::Url state_url;
      if (!state_url.initialize(state, false)) {
        sendUnauthorizedResponse();
        return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
      }
      // Avoid infinite redirect storm
      if (absl::StartsWith(state_url.pathAndQueryParams(), config_->callbackPath())) {
        sendUnauthorizedResponse();
        return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
      }
      Http::ResponseHeaderMapPtr response_headers{
          Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
              {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))},
               {Http::Headers::get().Location, state}})};
      decoder_callbacks_->encodeHeaders(std::move(response_headers), true);
    }

    // Continue on with the filter stack.
    return Http::FilterHeadersStatus::Continue;
  }

  // If a bearer token is supplied as a header or param, we ingest it here and kick off the
  // user resolution immediately. Note this comes after HMAC validation, so technically this
  // header is sanitized in a way, as the validation check forces the correct Bearer Cookie value.
  access_token_ = extractAccessToken(headers, Token);
  if (!access_token_.empty()) {
    found_bearer_token_ = true;
    request_headers_ = &headers;
    oauth_client_->asyncGetIdentity(access_token_);

    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  // If no access token and this isn't the callback URI, redirect to acquire credentials.
  //
  // The following conditional could be replaced with a regex pattern-match,
  // if we're concerned about matching strictly `/_oauth`.
  if (!absl::StartsWith(path_str, config_->callbackPath())) {
    Http::ResponseHeaderMapPtr response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
        {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))}})};

    // Construct the correct scheme. We default to https since this is a requirement for OAuth to
    // succeed. However, if a downstream client explicitly declares the "http" scheme for whatever
    // reason, we also use "http" when constructing our redirect uri to the authorization server.
    auto scheme = Http::Headers::get().SchemeValues.Https;

    const auto* scheme_header = headers.Scheme();
    if ((scheme_header != nullptr &&
         scheme_header->value().getStringView() == Http::Headers::get().SchemeValues.Http)) {
      scheme = Http::Headers::get().SchemeValues.Http;
    }

    const std::string base_path = absl::StrCat(scheme, "://", host_);
    const std::string callback_path = base_path + config_->callbackPath();
    const std::string state_path = absl::StrCat(base_path, headers.Path()->value().getStringView());

    const std::string escaped_redirect_uri =
        absl::StrReplaceAll(callback_path, escapedReplacements());
    const std::string escaped_state = absl::StrReplaceAll(state_path, escapedReplacements());

    const std::string new_url =
        fmt::format(authClusterUriParameters(), config_->oauthServerHostname(), config_->clientId(),
                    escaped_redirect_uri, escaped_state);
    response_headers->setReferenceKey(Http::Headers::get().Location, new_url);
    decoder_callbacks_->encodeHeaders(std::move(response_headers), true);

    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  // At this point, we *are* on /_oauth. We believe this request comes from the authorization
  // server and we expect the query strings to contain the information required to get the access
  // token
  Http::Utility::QueryParams query_parameters = Http::Utility::parseQueryString(path_str);
  if (query_parameters.find(queryParamsError()) != query_parameters.end()) {
    sendUnauthorizedResponse();
    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  // if the data we need is not present on the URL, stop execution
  if (query_parameters.find(queryParamsCode()) == query_parameters.end() ||
      query_parameters.find(queryParamsState()) == query_parameters.end()) {
    sendUnauthorizedResponse();
    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  auth_code_ = query_parameters.at(queryParamsCode());
  state_ = absl::StrReplaceAll(query_parameters.at(queryParamsState()), unescapedReplacements());

  Http::Utility::Url state_url;
  if (!state_url.initialize(state_, false)) {
    sendUnauthorizedResponse();
    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }
  const auto scheme = state_url.scheme();

  const std::string cb_url = absl::StrCat(scheme, "://", host_, config_->callbackPath());
  oauth_client_->asyncGetAccessToken(auth_code_, config_->clientId(), config_->clientSecret(),
                                     cb_url);

  // pause while we await the next step from the OAuth server
  return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
}

// Always sanitize these legacy Oauth headers that may be maliciously injected to mess with
// Jenkins's built-in authentication. These will be readded later in the filter after
// a successful auth flow.
void OAuth2Filter::sanitizeXForwardedOauthHeaders(Http::RequestHeaderMap& headers) {
  headers.remove(xForwardedUser());
}

// Set the legacy Oauth headers.
void OAuth2Filter::setXForwardedOauthHeaders(Http::RequestHeaderMap& headers,
                                             const std::string& token,
                                             const std::string& username) {
  // Add the x-forwarded headers after inspecting the corresponding cookie values.
  headers.setReferenceKey(Http::Headers::get().Authorization, absl::StrCat("Bearer ", token));
  headers.setReferenceKey(xForwardedUser(), username);
}

// Defines a sequence of checks determining whether we should initiate a new OAuth flow or skip to
// the next filter in the chain.
bool OAuth2Filter::canSkipOAuth(Http::RequestHeaderMap& headers) const {
  // We can skip OAuth if the supplied HMAC cookie is valid. Apply the OAuth details as headers
  // if we successfully validate the cookie.
  validator_->setParams(headers, config_->tokenSecret());
  if (validator_->isValid()) {
    config_->stats().oauth_success_.inc();
    setXForwardedOauthHeaders(headers, validator_->token(), validator_->username());
    return true;
  }

  // Skip authentication for HTTP method OPTIONS for CORS preflight requests to skip forbidden
  // redirects to the auth server. Must be opted in from the proto configuration.
  if (config_->passThroughOptionsMethod()) {
    const Http::HeaderEntry* method_header = headers.Method();
    if (method_header != nullptr &&
        method_header->value().getStringView() == Http::Headers::get().MethodValues.Options) {
      return true;
    }
  }

  return false;
}

/**
 * Modifies the state of the filter by adding response headers to the decoder_callbacks
 */
Http::FilterHeadersStatus OAuth2Filter::signOutUser(const Http::RequestHeaderMap& headers) {
  Http::ResponseHeaderMapPtr response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))}})};

  const std::string new_path =
      absl::StrCat(headers.ForwardedProto()->value().getStringView(), "://", host_, "/");
  response_headers->addReference(Http::Headers::get().SetCookie, signoutCookieValue());
  response_headers->addReference(Http::Headers::get().SetCookie, signoutBearerTokenValue());
  response_headers->setReferenceKey(Http::Headers::get().Location, new_path);
  decoder_callbacks_->encodeHeaders(std::move(response_headers), true);

  return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
}

// First callback in OAuth.
void OAuth2Filter::onGetAccessTokenSuccess(const std::string& access_code,
                                           const std::string& expires_in) {
  std::istringstream iss(expires_in);
  time_t expires_in_t;
  iss >> expires_in_t;
  time_t new_epoch = std::time(nullptr) + expires_in_t;

  access_token_ = access_code;
  new_expires_ = std::to_string(new_epoch);

  oauth_client_->asyncGetIdentity(access_token_);
}

// Second callback in OAuth.
void OAuth2Filter::onGetIdentitySuccess(const std::string& username) {
  username_ = username;

  // We have fully completed the entire OAuth flow, whether through Authorization header or from
  // user redirection to the auth server.
  if (found_bearer_token_) {
    setXForwardedOauthHeaders(*request_headers_, access_token_, username_);
    config_->stats().oauth_success_.inc();
    decoder_callbacks_->continueDecoding();
    return;
  }

  std::string token_payload;
  if (config_->forwardBearerToken()) {
    token_payload = absl::StrCat(host_, username_, new_expires_, access_token_);
  } else {
    token_payload = absl::StrCat(host_, username_, new_expires_);
  }

  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();

  auto token_secret = config_->tokenSecret();
  std::vector<uint8_t> token_secret_vec(token_secret.begin(), token_secret.end());
  const std::string pre_encoded_token =
      Hex::encode(crypto_util.getSha256Hmac(token_secret_vec, token_payload));
  std::string encoded_token;
  absl::Base64Escape(pre_encoded_token, &encoded_token);

  // We use HTTP Only cookies for the HMAC and Expiry. These should be more private and restricted
  // from free view in scripts. However, as username is extracted by the underlying
  // service, we can expose these values.
  const std::string cookie_tail = fmt::format(cookieTailFormatString(), new_expires_);
  const std::string cookie_tail_http_only =
      fmt::format(cookieTailHttpOnlyFormatString(), new_expires_);

  /**
   * At this point we have all of the pieces needed to authenticate a user that did not originally
   * have a bearer access token. Now, we construct a redirect request to return the user to their
   * previous state and additionally set the OAuth cookies in browser.
   * The redirection should result in successfully passing this filer.
   */
  Http::ResponseHeaderMapPtr response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))}})};

  static const Http::LowerCaseString set_cookie_key = Http::Headers::get().SetCookie;

  response_headers->addReferenceKey(
      set_cookie_key, absl::StrCat("OauthHMAC=", encoded_token, cookie_tail_http_only));
  response_headers->addReferenceKey(
      set_cookie_key, absl::StrCat("OauthExpires=", new_expires_, cookie_tail_http_only));
  response_headers->addReferenceKey(set_cookie_key,
                                    absl::StrCat("OauthUsername=", username_, cookie_tail));

  // If opted-in, we also create a new Bearer cookie for the authorization token provided by the
  // auth server.
  if (config_->forwardBearerToken()) {
    response_headers->addReferenceKey(set_cookie_key,
                                      absl::StrCat("BearerToken=", access_token_, cookie_tail));
  }

  response_headers->setReferenceKey(Http::Headers::get().Location, state_);

  decoder_callbacks_->encodeHeaders(std::move(response_headers), true);
  config_->stats().oauth_success_.inc();
  decoder_callbacks_->continueDecoding();
}

void OAuth2Filter::sendUnauthorizedResponse() {
  config_->stats().oauth_failure_.inc();
  decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, unauthorizedBodyMessage(), nullptr,
                                     absl::nullopt, EMPTY_STRING);
}

} // namespace Oauth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
