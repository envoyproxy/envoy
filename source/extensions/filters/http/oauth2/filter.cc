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
#include "source/common/common/matchers.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

namespace {
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);

// Deleted OauthHMAC cookie.
constexpr const char* SignoutCookieValue =
    "{}=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT";

// Deleted BearerToken cookie.
constexpr const char* SignoutBearerTokenValue =
    "{}=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT";

constexpr absl::string_view SignoutIdTokenValue =
    "IdToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT";

constexpr absl::string_view SignoutRefreshTokenValue =
    "RefreshToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT";

constexpr const char* CookieTailFormatString = ";version=1;path=/;Max-Age={};secure";

constexpr const char* CookieTailHttpOnlyFormatString =
    ";version=1;path=/;Max-Age={};secure;HttpOnly";

const char* AuthorizationEndpointFormat =
    "{}?client_id={}&scope={}&response_type=code&redirect_uri={}&state={}";

constexpr absl::string_view UnauthorizedBodyMessage = "OAuth flow failed.";

const std::string& queryParamsError() { CONSTRUCT_ON_FIRST_USE(std::string, "error"); }
const std::string& queryParamsCode() { CONSTRUCT_ON_FIRST_USE(std::string, "code"); }
const std::string& queryParamsState() { CONSTRUCT_ON_FIRST_USE(std::string, "state"); }

constexpr absl::string_view REDIRECT_RACE = "oauth.race_redirect";
constexpr absl::string_view REDIRECT_LOGGED_IN = "oauth.logged_in";
constexpr absl::string_view REDIRECT_FOR_CREDENTIALS = "oauth.missing_credentials";
constexpr absl::string_view SIGN_OUT = "oauth.sign_out";
constexpr absl::string_view DEFAULT_AUTH_SCOPE = "user";

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
  for (const auto& resource : resources_protos) {
    result += "&resource=" + Http::Utility::PercentEncoding::encode(resource, ":/=&? ");
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

} // namespace

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2Config& proto_config,
    Upstream::ClusterManager& cluster_manager, std::shared_ptr<SecretReader> secret_reader,
    Stats::Scope& scope, const std::string& stats_prefix)
    : oauth_token_endpoint_(proto_config.token_endpoint()),
      authorization_endpoint_(proto_config.authorization_endpoint()),
      client_id_(proto_config.credentials().client_id()),
      redirect_uri_(proto_config.redirect_uri()),
      redirect_matcher_(proto_config.redirect_path_matcher()),
      signout_path_(proto_config.signout_path()), secret_reader_(secret_reader),
      stats_(FilterConfig::generateStats(stats_prefix, scope)),
      encoded_auth_scopes_(Http::Utility::PercentEncoding::encode(
          absl::StrJoin(authScopesList(proto_config.auth_scopes()), " "), ":/=&? ")),
      encoded_resource_query_params_(encodeResourceList(proto_config.resources())),
      forward_bearer_token_(proto_config.forward_bearer_token()),
      pass_through_header_matchers_(headerMatchers(proto_config.pass_through_matcher())),
      cookie_names_(proto_config.credentials().cookie_names()),
      auth_type_(getAuthType(proto_config.auth_type())) {
  if (!cluster_manager.clusters().hasCluster(oauth_token_endpoint_.cluster())) {
    throw EnvoyException(fmt::format("OAuth2 filter: unknown cluster '{}' in config. Please "
                                     "specify which cluster to direct OAuth requests to.",
                                     oauth_token_endpoint_.cluster()));
  }
}

FilterStats FilterConfig::generateStats(const std::string& prefix, Stats::Scope& scope) {
  return {ALL_OAUTH_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

void OAuth2CookieValidator::setParams(const Http::RequestHeaderMap& headers,
                                      const std::string& secret) {
  const auto& cookies = Http::Utility::parseCookies(headers, [this](absl::string_view key) -> bool {
    return key == cookie_names_.oauth_expires_ || key == cookie_names_.bearer_token_ ||
           key == cookie_names_.oauth_hmac_ || key == "IdToken" || key == "RefreshToken";
  });

  expires_ = findValue(cookies, cookie_names_.oauth_expires_);
  token_ = findValue(cookies, cookie_names_.bearer_token_);
  id_token_ = findValue(cookies, "IdToken");
  refresh_token_ = findValue(cookies, "RefreshToken");
  hmac_ = findValue(cookies, cookie_names_.oauth_hmac_);
  host_ = headers.Host()->value().getStringView();

  secret_.assign(secret.begin(), secret.end());
}

bool OAuth2CookieValidator::hmacIsValid() const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto hmac_payload = absl::StrCat(host_, expires_, token_, id_token_, refresh_token_);
  const auto pre_encoded_hmac = Hex::encode(crypto_util.getSha256Hmac(secret_, hmac_payload));
  std::string encoded_hmac;
  absl::Base64Escape(pre_encoded_hmac, &encoded_hmac);

  return encoded_hmac == hmac_;
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
      oauth_client_(std::move(oauth_client)), config_(std::move(config)),
      time_source_(time_source) {

  oauth_client_->setCallbacks(*this);
}

const std::string& OAuth2Filter::bearerPrefix() const {
  CONSTRUCT_ON_FIRST_USE(std::string, "bearer ");
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
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth_header_passthrough_fix")) {
    for (const auto& matcher : config_->passThroughMatchers()) {
      if (matcher.matchesHeaders(headers)) {
        config_->stats().oauth_passthrough_.inc();
        return Http::FilterHeadersStatus::Continue;
      }
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
      Http::Utility::QueryParams query_parameters = Http::Utility::parseQueryString(path_str);

      const auto state =
          Http::Utility::PercentEncoding::decode(query_parameters.at(queryParamsState()));
      Http::Utility::Url state_url;
      if (!state_url.initialize(state, false)) {
        sendUnauthorizedResponse();
        return Http::FilterHeadersStatus::StopIteration;
      }
      // Avoid infinite redirect storm
      if (config_->redirectPathMatcher().match(state_url.pathAndQueryParams())) {
        sendUnauthorizedResponse();
        return Http::FilterHeadersStatus::StopIteration;
      }
      Http::ResponseHeaderMapPtr response_headers{
          Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
              {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))},
               {Http::Headers::get().Location, state}})};
      decoder_callbacks_->encodeHeaders(std::move(response_headers), true, REDIRECT_RACE);
    }

    // Continue on with the filter stack.
    return Http::FilterHeadersStatus::Continue;
  }

  // Save the request headers for later modification if needed.
  if (config_->forwardBearerToken()) {
    request_headers_ = &headers;
  }

  // If this isn't the callback URI, redirect to acquire credentials.
  //
  // The following conditional could be replaced with a regex pattern-match,
  // if we're concerned about strict matching against the callback path.
  if (!config_->redirectPathMatcher().match(path_str)) {
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
    const std::string state_path = absl::StrCat(base_path, headers.Path()->value().getStringView());
    const std::string escaped_state = Http::Utility::PercentEncoding::encode(state_path, ":/=&?");

    Formatter::FormatterImpl formatter(config_->redirectUri());
    const auto redirect_uri = formatter.format(headers, *Http::ResponseHeaderMapImpl::create(),
                                               *Http::ResponseTrailerMapImpl::create(),
                                               decoder_callbacks_->streamInfo(), "");
    const std::string escaped_redirect_uri =
        Http::Utility::PercentEncoding::encode(redirect_uri, ":/=&?");

    const std::string new_url = fmt::format(
        AuthorizationEndpointFormat, config_->authorizationEndpoint(), config_->clientId(),
        config_->encodedAuthScopes(), escaped_redirect_uri, escaped_state);

    response_headers->setLocation(new_url + config_->encodedResourceQueryParams());
    decoder_callbacks_->encodeHeaders(std::move(response_headers), true, REDIRECT_FOR_CREDENTIALS);

    config_->stats().oauth_unauthorized_rq_.inc();

    return Http::FilterHeadersStatus::StopIteration;
  }

  // At this point, we *are* on /_oauth. We believe this request comes from the authorization
  // server and we expect the query strings to contain the information required to get the access
  // token
  const auto query_parameters = Http::Utility::parseQueryString(path_str);
  if (query_parameters.find(queryParamsError()) != query_parameters.end()) {
    sendUnauthorizedResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }

  // if the data we need is not present on the URL, stop execution
  if (query_parameters.find(queryParamsCode()) == query_parameters.end() ||
      query_parameters.find(queryParamsState()) == query_parameters.end()) {
    sendUnauthorizedResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }

  auth_code_ = query_parameters.at(queryParamsCode());
  state_ = Http::Utility::PercentEncoding::decode(query_parameters.at(queryParamsState()));

  Http::Utility::Url state_url;
  if (!state_url.initialize(state_, false)) {
    sendUnauthorizedResponse();
    return Http::FilterHeadersStatus::StopIteration;
  }

  Formatter::FormatterImpl formatter(config_->redirectUri());
  const auto redirect_uri = formatter.format(headers, *Http::ResponseHeaderMapImpl::create(),
                                             *Http::ResponseTrailerMapImpl::create(),
                                             decoder_callbacks_->streamInfo(), "");
  oauth_client_->asyncGetAccessToken(auth_code_, config_->clientId(), config_->clientSecret(),
                                     redirect_uri, config_->authType());

  // pause while we await the next step from the OAuth server
  return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
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
    return true;
  }
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth_header_passthrough_fix")) {
    for (const auto& matcher : config_->passThroughMatchers()) {
      if (matcher.matchesHeaders(headers)) {
        return true;
      }
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

  const std::string new_path = absl::StrCat(headers.getSchemeValue(), "://", host_, "/");
  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      fmt::format(SignoutCookieValue, config_->cookieNames().oauth_hmac_));
  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      fmt::format(SignoutBearerTokenValue, config_->cookieNames().bearer_token_));
  response_headers->addReferenceKey(Http::Headers::get().SetCookie, SignoutIdTokenValue);
  response_headers->addReferenceKey(Http::Headers::get().SetCookie, SignoutRefreshTokenValue);
  response_headers->setLocation(new_path);
  decoder_callbacks_->encodeHeaders(std::move(response_headers), true, SIGN_OUT);

  return Http::FilterHeadersStatus::StopIteration;
}

void OAuth2Filter::onGetAccessTokenSuccess(const std::string& access_code,
                                           const std::string& id_token,
                                           const std::string& refresh_token,
                                           std::chrono::seconds expires_in) {
  access_token_ = access_code;
  id_token_ = id_token;
  refresh_token_ = refresh_token;

  const auto new_epoch = time_source_.systemTime() + expires_in;
  new_expires_ = std::to_string(
      std::chrono::duration_cast<std::chrono::seconds>(new_epoch.time_since_epoch()).count());

  finishFlow();
}

void OAuth2Filter::finishFlow() {
  std::string token_payload;
  if (config_->forwardBearerToken()) {
    token_payload = absl::StrCat(host_, new_expires_, access_token_, id_token_, refresh_token_);
  } else {
    token_payload = absl::StrCat(host_, new_expires_);
  }

  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();

  auto token_secret = config_->tokenSecret();
  std::vector<uint8_t> token_secret_vec(token_secret.begin(), token_secret.end());
  const std::string pre_encoded_token =
      Hex::encode(crypto_util.getSha256Hmac(token_secret_vec, token_payload));
  std::string encoded_token;
  absl::Base64Escape(pre_encoded_token, &encoded_token);

  // We use HTTP Only cookies for the HMAC and Expiry.
  const std::string cookie_tail = fmt::format(CookieTailFormatString, new_expires_);
  const std::string cookie_tail_http_only =
      fmt::format(CookieTailHttpOnlyFormatString, new_expires_);

  // At this point we have all of the pieces needed to authorize a user.
  // Now, we construct a redirect request to return the user to their
  // previous state and additionally set the OAuth cookies in browser.
  // The redirection should result in successfully passing this filter.
  Http::ResponseHeaderMapPtr response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))}})};

  const CookieNames& cookie_names = config_->cookieNames();

  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      absl::StrCat(cookie_names.oauth_hmac_, "=", encoded_token, cookie_tail_http_only));
  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      absl::StrCat(cookie_names.oauth_expires_, "=", new_expires_, cookie_tail_http_only));

  // If opted-in, we also create a new Bearer cookie for the authorization token provided by the
  // auth server.
  if (config_->forwardBearerToken()) {
    response_headers->addReferenceKey(
        Http::Headers::get().SetCookie,
        absl::StrCat(cookie_names.bearer_token_, "=", access_token_, cookie_tail));
    if (id_token_ != EMPTY_STRING) {
      response_headers->addReferenceKey(Http::Headers::get().SetCookie,
                                        absl::StrCat("IdToken=", id_token_, cookie_tail));
    }

    if (refresh_token_ != EMPTY_STRING) {
      response_headers->addReferenceKey(Http::Headers::get().SetCookie,
                                        absl::StrCat("RefreshToken=", refresh_token_, cookie_tail));
    }
  }

  response_headers->setLocation(state_);

  decoder_callbacks_->encodeHeaders(std::move(response_headers), true, REDIRECT_LOGGED_IN);
  config_->stats().oauth_success_.inc();
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
