#include "source/extensions/filters/http/oauth2/filter.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

namespace {
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);

constexpr const char* CookieDeleteFormatString =
    "{}=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT";
constexpr const char* CookieTailFormatString = ";version=1;path=/;Max-Age={};secure";
constexpr const char* CookieTailHttpOnlyFormatString =
    ";version=1;path=/;Max-Age={};secure;HttpOnly";

constexpr absl::string_view UnauthorizedBodyMessage = "OAuth flow failed.";

const std::string& queryParamsError() { CONSTRUCT_ON_FIRST_USE(std::string, "error"); }
const std::string& queryParamsCode() { CONSTRUCT_ON_FIRST_USE(std::string, "code"); }
const std::string& queryParamsState() { CONSTRUCT_ON_FIRST_USE(std::string, "state"); }

constexpr absl::string_view REDIRECT_RACE = "oauth.race_redirect";
constexpr absl::string_view REDIRECT_LOGGED_IN = "oauth.logged_in";
constexpr absl::string_view REDIRECT_FOR_CREDENTIALS = "oauth.missing_credentials";
constexpr absl::string_view SIGN_OUT = "oauth.sign_out";
constexpr absl::string_view DEFAULT_AUTH_SCOPE = "user";

constexpr absl::string_view HmacPayloadSeparator = "\n";

template <class T>
std::vector<Http::HeaderUtility::HeaderData> headerMatchers(const T& matcher_protos) {
  std::vector<Http::HeaderUtility::HeaderData> matchers;
  matchers.reserve(matcher_protos.size());

  for (const auto& proto : matcher_protos) {
    matchers.emplace_back(proto);
  }

  return matchers;
}

// Transforms the proto list of 'auth_scopes' into a vector of std::string, also
// handling the default value logic.
std::vector<std::string>
authScopesList(const Protobuf::RepeatedPtrField<std::string>& auth_scopes_protos) {
  std::vector<std::string> scopes;

  // If 'auth_scopes' is empty it must return a list with the default value.
  if (auth_scopes_protos.empty()) {
    scopes.emplace_back(DEFAULT_AUTH_SCOPE);
  } else {
    scopes.reserve(auth_scopes_protos.size());

    for (const auto& scope : auth_scopes_protos) {
      scopes.emplace_back(scope);
    }
  }
  return scopes;
}

// Transforms the proto list into encoded resource params
// Takes care of percentage encoding http and https is needed
std::string encodeResourceList(const Protobuf::RepeatedPtrField<std::string>& resources_protos) {
  std::string result = "";
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth_use_url_encoding")) {
    for (const auto& resource : resources_protos) {
      result += "&resource=" + Http::Utility::PercentEncoding::urlEncodeQueryParameter(resource);
    }
  } else {
    for (const auto& resource : resources_protos) {
      result += "&resource=" + Http::Utility::PercentEncoding::encode(resource, ":/=&? ");
    }
  }
  return result;
}

// Sets the auth token as the Bearer token in the authorization header.
void setBearerToken(Http::RequestHeaderMap& headers, const std::string& token) {
  headers.setInline(authorization_handle.handle(), absl::StrCat("Bearer ", token));
}

std::string findValue(const absl::flat_hash_map<std::string, std::string>& map,
                      const std::string& key) {
  const auto value_it = map.find(key);
  return value_it != map.end() ? value_it->second : EMPTY_STRING;
}

AuthType
getAuthType(envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType auth_type) {
  switch (auth_type) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
      OAuth2Config_AuthType_BASIC_AUTH:
    return AuthType::BasicAuth;
  case envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType::
      OAuth2Config_AuthType_URL_ENCODED_BODY:
  default:
    return AuthType::UrlEncodedBody;
  }
}

Http::Utility::QueryParamsMulti buildAutorizationQueryParams(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2Config& proto_config) {
  auto query_params =
      Http::Utility::QueryParamsMulti::parseQueryString(proto_config.authorization_endpoint());
  query_params.overwrite("client_id", proto_config.credentials().client_id());
  query_params.overwrite("response_type", "code");
  std::string scopes_list = absl::StrJoin(authScopesList(proto_config.auth_scopes()), " ");
  query_params.overwrite(
      "scope", Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth_use_url_encoding")
                   ? Http::Utility::PercentEncoding::urlEncodeQueryParameter(scopes_list)
                   : Http::Utility::PercentEncoding::encode(scopes_list, ":/=&? "));
  return query_params;
}

std::string encodeHmacHexBase64(const std::vector<uint8_t>& secret, absl::string_view host,
                                absl::string_view expires, absl::string_view token = "",
                                absl::string_view id_token = "",
                                absl::string_view refresh_token = "") {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto hmac_payload =
      absl::StrJoin({host, expires, token, id_token, refresh_token}, HmacPayloadSeparator);
  std::string encoded_hmac;
  absl::Base64Escape(Hex::encode(crypto_util.getSha256Hmac(secret, hmac_payload)), &encoded_hmac);
  return encoded_hmac;
}

std::string encodeHmacBase64(const std::vector<uint8_t>& secret, absl::string_view host,
                             absl::string_view expires, absl::string_view token = "",
                             absl::string_view id_token = "",
                             absl::string_view refresh_token = "") {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto hmac_payload =
      absl::StrJoin({host, expires, token, id_token, refresh_token}, HmacPayloadSeparator);

  std::string base64_encoded_hmac;
  std::vector<uint8_t> hmac_result = crypto_util.getSha256Hmac(secret, hmac_payload);
  std::string hmac_string(hmac_result.begin(), hmac_result.end());
  absl::Base64Escape(hmac_string, &base64_encoded_hmac);
  return base64_encoded_hmac;
}

std::string encodeHmac(const std::vector<uint8_t>& secret, absl::string_view host,
                       absl::string_view expires, absl::string_view token = "",
                       absl::string_view id_token = "", absl::string_view refresh_token = "") {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.hmac_base64_encoding_only")) {
    return encodeHmacBase64(secret, host, expires, token, id_token, refresh_token);
  } else {
    return encodeHmacHexBase64(secret, host, expires, token, id_token, refresh_token);
  }
}

} // namespace

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2Config& proto_config,
    Upstream::ClusterManager& cluster_manager, std::shared_ptr<SecretReader> secret_reader,
    Stats::Scope& scope, const std::string& stats_prefix)
    : oauth_token_endpoint_(proto_config.token_endpoint()),
      authorization_endpoint_(proto_config.authorization_endpoint()),
      authorization_query_params_(buildAutorizationQueryParams(proto_config)),
      client_id_(proto_config.credentials().client_id()),
      redirect_uri_(proto_config.redirect_uri()),
      redirect_matcher_(proto_config.redirect_path_matcher()),
      signout_path_(proto_config.signout_path()), secret_reader_(secret_reader),
      stats_(FilterConfig::generateStats(stats_prefix, scope)),
      encoded_resource_query_params_(encodeResourceList(proto_config.resources())),
      forward_bearer_token_(proto_config.forward_bearer_token()),
      pass_through_header_matchers_(headerMatchers(proto_config.pass_through_matcher())),
      cookie_names_(proto_config.credentials().cookie_names()),
      auth_type_(getAuthType(proto_config.auth_type())),
      use_refresh_token_(proto_config.use_refresh_token().value()) {
  if (!cluster_manager.clusters().hasCluster(oauth_token_endpoint_.cluster())) {
    throw EnvoyException(fmt::format("OAuth2 filter: unknown cluster '{}' in config. Please "
                                     "specify which cluster to direct OAuth requests to.",
                                     oauth_token_endpoint_.cluster()));
  }
  if (!authorization_endpoint_url_.initialize(authorization_endpoint_,
                                              /*is_connect_request=*/false)) {
    throw EnvoyException(
        fmt::format("OAuth2 filter: invalid authorization endpoint URL '{}' in config.",
                    authorization_endpoint_));
  }
}

FilterStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  return {ALL_OAUTH_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

void OAuth2CookieValidator::setParams(const Http::RequestHeaderMap& headers,
                                      const std::string& secret) {
  const auto& cookies = Http::Utility::parseCookies(headers, [this](absl::string_view key) -> bool {
    return key == cookie_names_.oauth_expires_ || key == cookie_names_.bearer_token_ ||
           key == cookie_names_.oauth_hmac_ || key == cookie_names_.id_token_ ||
           key == cookie_names_.refresh_token_;
  });

  expires_ = findValue(cookies, cookie_names_.oauth_expires_);
  token_ = findValue(cookies, cookie_names_.bearer_token_);
  id_token_ = findValue(cookies, cookie_names_.id_token_);
  refresh_token_ = findValue(cookies, cookie_names_.refresh_token_);
  hmac_ = findValue(cookies, cookie_names_.oauth_hmac_);
  host_ = headers.Host()->value().getStringView();

  secret_.assign(secret.begin(), secret.end());
}

bool OAuth2CookieValidator::canUpdateTokenByRefreshToken() const {
  return (!token_.empty() && !refresh_token_.empty());
}

bool OAuth2CookieValidator::hmacIsValid() const {
  return (
      (encodeHmacBase64(secret_, host_, expires_, token_, id_token_, refresh_token_) == hmac_) ||
      (encodeHmacHexBase64(secret_, host_, expires_, token_, id_token_, refresh_token_) == hmac_));
}

bool OAuth2CookieValidator::timestampIsValid() const {
  uint64_t expires;
  if (!absl::SimpleAtoi(expires_, &expires)) {
    return false;
  }

  const auto current_epoch = time_source_.systemTime().time_since_epoch();
  return std::chrono::seconds(expires) > current_epoch;
}

bool OAuth2CookieValidator::isValid() const { return hmacIsValid() && timestampIsValid(); }

OAuth2Filter::OAuth2Filter(FilterConfigSharedPtr config,
                           std::unique_ptr<OAuth2Client>&& oauth_client, TimeSource& time_source)
    : validator_(std::make_shared<OAuth2CookieValidator>(time_source, config->cookieNames())),
      was_refresh_token_flow_(false), oauth_client_(std::move(oauth_client)),
      config_(std::move(config)), time_source_(time_source) {

  oauth_client_->setCallbacks(*this);
}

/**
 * primary cases:
 * 1) pass through header is matching
 * 2) user is signing out
 * 3) /_oauth redirect
 * 4) user is authorized
 * 5) user is unauthorized
 */
Http::FilterHeadersStatus OAuth2Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // Skip Filter and continue chain if a Passthrough header is matching
  // Must be done before the sanitation of the authorization header,
  // otherwise the authorization header might be altered or removed
  for (const auto& matcher : config_->passThroughMatchers()) {
    if (matcher.matchesHeaders(headers)) {
      config_->stats().oauth_passthrough_.inc();
      return Http::FilterHeadersStatus::Continue;
    }
  }

  // Sanitize the Authorization header, since we have no way to validate its content. Also,
  // if token forwarding is enabled, this header will be set based on what is on the HMAC cookie
  // before forwarding the request upstream.
  headers.removeInline(authorization_handle.handle());

  // The following 2 headers are guaranteed for regular requests. The asserts are helpful when
  // writing test code to not forget these important variables in mock requests
  const Http::HeaderEntry* host_header = headers.Host();
  ASSERT(host_header != nullptr);
  host_ = host_header->value().getStringView();

  const Http::HeaderEntry* path_header = headers.Path();
  ASSERT(path_header != nullptr);
  const absl::string_view path_str = path_header->value().getStringView();

  // We should check if this is a sign out request.
  if (config_->signoutPath().match(path_header->value().getStringView())) {
    return signOutUser(headers);
  }

  if (canSkipOAuth(headers)) {
    // Update the path header with the query string parameters after a successful OAuth login.
    // This is necessary if a website requests multiple resources which get redirected to the
    // auth server. A cached login on the authorization server side will set cookies
    // correctly but cause a race condition on future requests that have their location set
    // to the callback path.

    if (config_->redirectPathMatcher().match(path_str)) {
      Http::Utility::QueryParamsMulti query_parameters =
          Http::Utility::QueryParamsMulti::parseQueryString(path_str);

      auto stateVal = query_parameters.getFirstValue(queryParamsState());
      if (!stateVal.has_value()) {
        ENVOY_LOG(error, "state query param does not exist: \n{}", query_parameters.data());
        sendUnauthorizedResponse();
        return Http::FilterHeadersStatus::StopIteration;
      }

      std::string state;
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth_use_url_encoding")) {
        state = Http::Utility::PercentEncoding::urlDecodeQueryParameter(stateVal.value());
      } else {
        state = Http::Utility::PercentEncoding::decode(stateVal.value());
      }
      Http::Utility::Url state_url;
      if (!state_url.initialize(state, false)) {
        ENVOY_LOG(debug, "state url {} can not be initialized", state_url.toString());
        sendUnauthorizedResponse();
        return Http::FilterHeadersStatus::StopIteration;
      }
      // Avoid infinite redirect storm
      if (config_->redirectPathMatcher().match(state_url.pathAndQueryParams())) {
        ENVOY_LOG(debug, "state url query params {} does not match redirect config",
                  state_url.pathAndQueryParams());
        sendUnauthorizedResponse();
        return Http::FilterHeadersStatus::StopIteration;
      }
      Http::ResponseHeaderMapPtr response_headers{
          Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
              {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))},
               {Http::Headers::get().Location, state}})};
      decoder_callbacks_->encodeHeaders(std::move(response_headers), true, REDIRECT_RACE);
      return Http::FilterHeadersStatus::StopIteration;
    }

    // Continue on with the filter stack.
    return Http::FilterHeadersStatus::Continue;
  }

  // Save the request headers for later modification if needed.
  request_headers_ = &headers;
  // If this isn't the callback URI, redirect to acquire credentials.
  //
  // The following conditional could be replaced with a regex pattern-match,
  // if we're concerned about strict matching against the callback path.
  if (!config_->redirectPathMatcher().match(path_str)) {

    // Check if we can update the access token via a refresh token.
    if (config_->useRefreshToken() && validator_->canUpdateTokenByRefreshToken()) {
      // try to update access token by refresh token
      oauth_client_->asyncRefreshAccessToken(validator_->refreshToken(), config_->clientId(),
                                             config_->clientSecret(), config_->authType());
      // pause while we await the next step from the OAuth server
      return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
    }

    ENVOY_LOG(debug, "path {} does not match with redirect matcher. redirecting to OAuth server.",
              path_str);
    redirectToOAuthServer(headers);
    return Http::FilterHeadersStatus::StopIteration;
  }

  // At this point, we *are* on /_oauth. We believe this request comes from the authorization
  // server and we expect the query strings to contain the information required to get the access
  // token
  const auto query_parameters = Http::Utility::QueryParamsMulti::parseQueryString(path_str);
  if (query_parameters.getFirstValue(queryParamsError()).has_value()) {
    sendUnauthorizedResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }

  // if the data we need is not present on the URL, stop execution
  auto codeVal = query_parameters.getFirstValue(queryParamsCode());
  auto stateVal = query_parameters.getFirstValue(queryParamsState());
  if (!codeVal.has_value() || !stateVal.has_value()) {
    sendUnauthorizedResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }

  auth_code_ = codeVal.value();
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth_use_url_encoding")) {
    state_ = Http::Utility::PercentEncoding::urlDecodeQueryParameter(stateVal.value());
  } else {
    state_ = Http::Utility::PercentEncoding::decode(stateVal.value());
  }

  Http::Utility::Url state_url;
  if (!state_url.initialize(state_, false)) {
    sendUnauthorizedResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }

  Formatter::FormatterImpl formatter(config_->redirectUri());
  const auto redirect_uri =
      formatter.formatWithContext({&headers}, decoder_callbacks_->streamInfo());
  oauth_client_->asyncGetAccessToken(auth_code_, config_->clientId(), config_->clientSecret(),
                                     redirect_uri, config_->authType());

  // pause while we await the next step from the OAuth server
  return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
}

Http::FilterHeadersStatus OAuth2Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (was_refresh_token_flow_) {
    addResponseCookies(headers, getEncodedToken());
    was_refresh_token_flow_ = false;
  }

  return Http::FilterHeadersStatus::Continue;
}

// Defines a sequence of checks determining whether we should initiate a new OAuth flow or skip to
// the next filter in the chain.
bool OAuth2Filter::canSkipOAuth(Http::RequestHeaderMap& headers) const {
  // We can skip OAuth if the supplied HMAC cookie is valid. Apply the OAuth details as headers
  // if we successfully validate the cookie.
  validator_->setParams(headers, config_->tokenSecret());
  if (validator_->isValid()) {
    config_->stats().oauth_success_.inc();
    if (config_->forwardBearerToken() && !validator_->token().empty()) {
      setBearerToken(headers, validator_->token());
    }
    ENVOY_LOG(debug, "skipping oauth flow due to valid hmac cookie");
    return true;
  }
  ENVOY_LOG(debug, "can not skip oauth flow");
  return false;
}

void OAuth2Filter::redirectToOAuthServer(Http::RequestHeaderMap& headers) const {
  Http::ResponseHeaderMapPtr response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))}})};

  // Construct the correct scheme. We default to https since this is a requirement for OAuth to
  // succeed. However, if a downstream client explicitly declares the "http" scheme for whatever
  // reason, we also use "http" when constructing our redirect uri to the authorization server.
  auto scheme = Http::Headers::get().SchemeValues.Https;

  if (Http::Utility::schemeIsHttp(headers.getSchemeValue())) {
    scheme = Http::Headers::get().SchemeValues.Http;
  }

  const std::string base_path = absl::StrCat(scheme, "://", host_);
  const std::string state_path = absl::StrCat(base_path, headers.Path()->value().getStringView());
  const std::string escaped_state =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth_use_url_encoding")
          ? Http::Utility::PercentEncoding::urlEncodeQueryParameter(state_path)
          : Http::Utility::PercentEncoding::encode(state_path, ":/=&?");

  Formatter::FormatterImpl formatter(config_->redirectUri());
  const auto redirect_uri =
      formatter.formatWithContext({&headers}, decoder_callbacks_->streamInfo());
  const std::string escaped_redirect_uri =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth_use_url_encoding")
          ? Http::Utility::PercentEncoding::urlEncodeQueryParameter(redirect_uri)
          : Http::Utility::PercentEncoding::encode(redirect_uri, ":/=&?");

  auto query_params = config_->authorizationQueryParams();
  query_params.overwrite("redirect_uri", escaped_redirect_uri);
  query_params.overwrite("state", escaped_state);
  // Copy the authorization endpoint URL to replace its query params.
  auto authorization_endpoint_url = config_->authorizationEndpointUrl();
  const std::string path_and_query_params = query_params.replaceQueryString(
      Http::HeaderString(authorization_endpoint_url.pathAndQueryParams()));
  authorization_endpoint_url.setPathAndQueryParams(path_and_query_params);
  const std::string new_url = authorization_endpoint_url.toString();

  response_headers->setLocation(new_url + config_->encodedResourceQueryParams());
  decoder_callbacks_->encodeHeaders(std::move(response_headers), true, REDIRECT_FOR_CREDENTIALS);

  config_->stats().oauth_unauthorized_rq_.inc();
}

/**
 * Modifies the state of the filter by adding response headers to the decoder_callbacks
 */
Http::FilterHeadersStatus OAuth2Filter::signOutUser(const Http::RequestHeaderMap& headers) {
  Http::ResponseHeaderMapPtr response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))}})};

  const std::string new_path = absl::StrCat(headers.getSchemeValue(), "://", host_, "/");
  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      fmt::format(CookieDeleteFormatString, config_->cookieNames().oauth_hmac_));
  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      fmt::format(CookieDeleteFormatString, config_->cookieNames().bearer_token_));
  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      fmt::format(CookieDeleteFormatString, config_->cookieNames().id_token_));
  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      fmt::format(CookieDeleteFormatString, config_->cookieNames().refresh_token_));
  response_headers->setLocation(new_path);
  decoder_callbacks_->encodeHeaders(std::move(response_headers), true, SIGN_OUT);

  return Http::FilterHeadersStatus::StopIteration;
}

void OAuth2Filter::updateTokens(const std::string& access_token, const std::string& id_token,
                                const std::string& refresh_token, std::chrono::seconds expires_in) {
  access_token_ = access_token;
  id_token_ = id_token;
  refresh_token_ = refresh_token;
  expires_in_ = std::to_string(expires_in.count());

  const auto new_epoch = time_source_.systemTime() + expires_in;
  new_expires_ = std::to_string(
      std::chrono::duration_cast<std::chrono::seconds>(new_epoch.time_since_epoch()).count());
}

std::string OAuth2Filter::getEncodedToken() const {
  auto token_secret = config_->tokenSecret();
  std::vector<uint8_t> token_secret_vec(token_secret.begin(), token_secret.end());
  std::string encoded_token;
  if (config_->forwardBearerToken()) {
    encoded_token =
        encodeHmac(token_secret_vec, host_, new_expires_, access_token_, id_token_, refresh_token_);
  } else {
    encoded_token = encodeHmac(token_secret_vec, host_, new_expires_);
  }
  return encoded_token;
}

void OAuth2Filter::onGetAccessTokenSuccess(const std::string& access_code,
                                           const std::string& id_token,
                                           const std::string& refresh_token,
                                           std::chrono::seconds expires_in) {
  updateTokens(access_code, id_token, refresh_token, expires_in);
  finishGetAccessTokenFlow();
}

void OAuth2Filter::onRefreshAccessTokenSuccess(const std::string& access_code,
                                               const std::string& id_token,
                                               const std::string& refresh_token,
                                               std::chrono::seconds expires_in) {
  ASSERT(config_->useRefreshToken());
  updateTokens(access_code, id_token, refresh_token, expires_in);
  finishRefreshAccessTokenFlow();
}

void OAuth2Filter::finishGetAccessTokenFlow() {
  // At this point we have all of the pieces needed to authorize a user.
  // Now, we construct a redirect request to return the user to their
  // previous state and additionally set the OAuth cookies in browser.
  // The redirection should result in successfully passing this filter.
  Http::ResponseHeaderMapPtr response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))}})};

  addResponseCookies(*response_headers, getEncodedToken());
  response_headers->setLocation(state_);

  decoder_callbacks_->encodeHeaders(std::move(response_headers), true, REDIRECT_LOGGED_IN);
  config_->stats().oauth_success_.inc();
}

void OAuth2Filter::finishRefreshAccessTokenFlow() {
  ASSERT(config_->useRefreshToken());
  // At this point we have updated all of the pieces need to authorize a user
  // We need to actualize keys in the cookie header of the current request related
  // with authorization. So, the upstream can use updated cookies for itself purpose
  const CookieNames& cookie_names = config_->cookieNames();

  absl::flat_hash_map<std::string, std::string> cookies =
      Http::Utility::parseCookies(*request_headers_);

  cookies.insert_or_assign(cookie_names.oauth_hmac_, getEncodedToken());
  cookies.insert_or_assign(cookie_names.oauth_expires_, new_expires_);

  if (config_->forwardBearerToken()) {
    cookies.insert_or_assign(cookie_names.bearer_token_, access_token_);
    if (!id_token_.empty()) {
      cookies.insert_or_assign(cookie_names.id_token_, id_token_);
    }
    if (!refresh_token_.empty()) {
      cookies.insert_or_assign(cookie_names.refresh_token_, refresh_token_);
    }
  }

  std::string new_cookies(absl::StrJoin(cookies, "; ", absl::PairFormatter("=")));
  request_headers_->addReferenceKey(Http::Headers::get().Cookie, new_cookies);
  if (config_->forwardBearerToken() && !access_token_.empty()) {
    setBearerToken(*request_headers_, access_token_);
  }

  was_refresh_token_flow_ = true;

  config_->stats().oauth_refreshtoken_success_.inc();
  config_->stats().oauth_success_.inc();
  decoder_callbacks_->continueDecoding();
}

void OAuth2Filter::onRefreshAccessTokenFailure() {
  config_->stats().oauth_refreshtoken_failure_.inc();
  // We failed to get an access token via the refresh token, so send the user to the oauth endpoint.
  redirectToOAuthServer(*request_headers_);
}

void OAuth2Filter::addResponseCookies(Http::ResponseHeaderMap& headers,
                                      const std::string& encoded_token) const {
  std::string max_age;
  if (Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.oauth_use_standard_max_age_value")) {
    max_age = expires_in_;
  } else {
    max_age = new_expires_;
  }

  // We use HTTP Only cookies.
  const std::string cookie_tail = fmt::format(CookieTailFormatString, max_age);
  const std::string cookie_tail_http_only = fmt::format(CookieTailHttpOnlyFormatString, max_age);
  const CookieNames& cookie_names = config_->cookieNames();

  headers.addReferenceKey(
      Http::Headers::get().SetCookie,
      absl::StrCat(cookie_names.oauth_hmac_, "=", encoded_token, cookie_tail_http_only));
  headers.addReferenceKey(
      Http::Headers::get().SetCookie,
      absl::StrCat(cookie_names.oauth_expires_, "=", new_expires_, cookie_tail_http_only));

  // If opted-in, we also create a new Bearer cookie for the authorization token provided by the
  // auth server.
  if (config_->forwardBearerToken()) {
    std::string cookie_attribute_httponly =
        Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth_make_token_cookie_httponly")
            ? cookie_tail_http_only
            : cookie_tail;
    headers.addReferenceKey(
        Http::Headers::get().SetCookie,
        absl::StrCat(cookie_names.bearer_token_, "=", access_token_, cookie_attribute_httponly));
    if (!id_token_.empty()) {
      headers.addReferenceKey(
          Http::Headers::get().SetCookie,
          absl::StrCat(cookie_names.id_token_, "=", id_token_, cookie_attribute_httponly));
    }

    if (!refresh_token_.empty()) {
      headers.addReferenceKey(Http::Headers::get().SetCookie,
                              absl::StrCat(cookie_names.refresh_token_, "=", refresh_token_,
                                           cookie_attribute_httponly));
    }
  }
}

void OAuth2Filter::sendUnauthorizedResponse() {
  config_->stats().oauth_failure_.inc();
  decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, UnauthorizedBodyMessage, nullptr,
                                     absl::nullopt, EMPTY_STRING);
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
