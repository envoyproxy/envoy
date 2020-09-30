#include "extensions/filters/http/oauth2/filter.h"

#include <algorithm>
#include <chrono>
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
#include "common/http/header_utility.h"
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
namespace Oauth2 {

namespace {
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);

constexpr absl::string_view SignoutCookieValue =
    "OauthHMAC=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT";

constexpr absl::string_view SignoutBearerTokenValue =
    "BearerToken=deleted; path=/; expires=Thu, 01 Jan 1970 00:00:00 GMT";

constexpr const char* CookieTailFormatString = ";version=1;path=/;Max-Age={};secure";

constexpr const char* CookieTailHttpOnlyFormatString =
    ";version=1;path=/;Max-Age={};secure;HttpOnly";

const char* AuthorizationEndpointFormat =
    "{}?client_id={}&scope=user&response_type=code&redirect_uri={}&state={}";

constexpr absl::string_view UnauthorizedBodyMessage = "OAuth flow failed.";

const std::string& queryParamsError() { CONSTRUCT_ON_FIRST_USE(std::string, "error"); }
const std::string& queryParamsCode() { CONSTRUCT_ON_FIRST_USE(std::string, "code"); }
const std::string& queryParamsState() { CONSTRUCT_ON_FIRST_USE(std::string, "state"); }

constexpr absl::string_view REDIRECT_RACE = "oauth.race_redirect";
constexpr absl::string_view REDIRECT_LOGGED_IN = "oauth.logged_in";
constexpr absl::string_view REDIRECT_FOR_CREDENTIALS = "oauth.missing_credentials";
constexpr absl::string_view SIGN_OUT = "oauth.sign_out";

template <class T>
std::vector<Http::HeaderUtility::HeaderData> headerMatchers(const T& matcher_protos) {
  std::vector<Http::HeaderUtility::HeaderData> matchers;
  matchers.reserve(matcher_protos.size());

  for (const auto& proto : matcher_protos) {
    matchers.emplace_back(proto);
  }

  return matchers;
}

// Sets the auth token as the Bearer token in the authorization header.
void setBearerToken(Http::RequestHeaderMap& headers, const std::string& token) {
  headers.setInline(authorization_handle.handle(), absl::StrCat("Bearer ", token));
}
} // namespace

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::oauth2::v3alpha::OAuth2Config& proto_config,
    Upstream::ClusterManager& cluster_manager, std::shared_ptr<SecretReader> secret_reader,
    Stats::Scope& scope, const std::string& stats_prefix)
    : oauth_token_endpoint_(proto_config.token_endpoint()),
      authorization_endpoint_(proto_config.authorization_endpoint()),
      client_id_(proto_config.credentials().client_id()),
      redirect_uri_(proto_config.redirect_uri()),
      redirect_matcher_(proto_config.redirect_path_matcher()),
      signout_path_(proto_config.signout_path()), secret_reader_(secret_reader),
      stats_(FilterConfig::generateStats(stats_prefix, scope)),
      forward_bearer_token_(proto_config.forward_bearer_token()),
      pass_through_header_matchers_(headerMatchers(proto_config.pass_through_matcher())) {
  if (!cluster_manager.get(oauth_token_endpoint_.cluster())) {
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
  expires_ = Http::Utility::parseCookieValue(headers, "OauthExpires");
  token_ = Http::Utility::parseCookieValue(headers, "BearerToken");
  hmac_ = Http::Utility::parseCookieValue(headers, "OauthHMAC");
  host_ = headers.Host()->value().getStringView();

  secret_.assign(secret.begin(), secret.end());
}

bool OAuth2CookieValidator::hmacIsValid() const {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto hmac_payload = absl::StrCat(host_, expires_, token_);
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
    : validator_(std::make_shared<OAuth2CookieValidator>(time_source)),
      oauth_client_(std::move(oauth_client)), config_(std::move(config)),
      time_source_(time_source) {

  oauth_client_->setCallbacks(*this);
}

const std::string& OAuth2Filter::bearerPrefix() const {
  CONSTRUCT_ON_FIRST_USE(std::string, "bearer ");
}

std::string OAuth2Filter::extractAccessToken(const Http::RequestHeaderMap& headers) const {
  ASSERT(headers.Path() != nullptr);

  // Start by looking for a bearer token in the Authorization header.
  const Http::HeaderEntry* authorization = headers.getInline(authorization_handle.handle());
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
  const auto param = params.find("token");
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
        return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
      }
      // Avoid infinite redirect storm
      if (config_->redirectPathMatcher().match(state_url.pathAndQueryParams())) {
        sendUnauthorizedResponse();
        return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
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

  // If a bearer token is supplied as a header or param, we ingest it here and kick off the
  // user resolution immediately. Note this comes after HMAC validation, so technically this
  // header is sanitized in a way, as the validation check forces the correct Bearer Cookie value.
  access_token_ = extractAccessToken(headers);
  if (!access_token_.empty()) {
    found_bearer_token_ = true;
    request_headers_ = &headers;
    finishFlow();

    return Http::FilterHeadersStatus::Continue;
  }

  // If no access token and this isn't the callback URI, redirect to acquire credentials.
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

    const std::string new_url =
        fmt::format(AuthorizationEndpointFormat, config_->authorizationEndpoint(),
                    config_->clientId(), escaped_redirect_uri, escaped_state);
    response_headers->setLocation(new_url);
    decoder_callbacks_->encodeHeaders(std::move(response_headers), true, REDIRECT_FOR_CREDENTIALS);

    config_->stats().oauth_unauthorized_rq_.inc();

    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  // At this point, we *are* on /_oauth. We believe this request comes from the authorization
  // server and we expect the query strings to contain the information required to get the access
  // token
  const auto query_parameters = Http::Utility::parseQueryString(path_str);
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
  state_ = Http::Utility::PercentEncoding::decode(query_parameters.at(queryParamsState()));

  Http::Utility::Url state_url;
  if (!state_url.initialize(state_, false)) {
    sendUnauthorizedResponse();
    return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
  }

  Formatter::FormatterImpl formatter(config_->redirectUri());
  const auto redirect_uri = formatter.format(headers, *Http::ResponseHeaderMapImpl::create(),
                                             *Http::ResponseTrailerMapImpl::create(),
                                             decoder_callbacks_->streamInfo(), "");
  oauth_client_->asyncGetAccessToken(auth_code_, config_->clientId(), config_->clientSecret(),
                                     redirect_uri);

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
    setBearerToken(headers, validator_->token());
    return true;
  }

  for (const auto& matcher : config_->passThroughMatchers()) {
    if (matcher.matchesHeaders(headers)) {
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
  response_headers->addReference(Http::Headers::get().SetCookie, SignoutCookieValue);
  response_headers->addReference(Http::Headers::get().SetCookie, SignoutBearerTokenValue);
  response_headers->setLocation(new_path);
  decoder_callbacks_->encodeHeaders(std::move(response_headers), true, SIGN_OUT);

  return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
}

void OAuth2Filter::onGetAccessTokenSuccess(const std::string& access_code,
                                           std::chrono::seconds expires_in) {
  access_token_ = access_code;

  const auto new_epoch = time_source_.systemTime() + expires_in;
  new_expires_ = std::to_string(
      std::chrono::duration_cast<std::chrono::seconds>(new_epoch.time_since_epoch()).count());

  finishFlow();
}

void OAuth2Filter::finishFlow() {

  // We have fully completed the entire OAuth flow, whether through Authorization header or from
  // user redirection to the auth server.
  if (found_bearer_token_) {
    setBearerToken(*request_headers_, access_token_);
    config_->stats().oauth_success_.inc();
    decoder_callbacks_->continueDecoding();
    return;
  }

  std::string token_payload;
  if (config_->forwardBearerToken()) {
    token_payload = absl::StrCat(host_, new_expires_, access_token_);
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

  // At this point we have all of the pieces needed to authorize a user that did not originally
  // have a bearer access token. Now, we construct a redirect request to return the user to their
  // previous state and additionally set the OAuth cookies in browser.
  // The redirection should result in successfully passing this filter.
  Http::ResponseHeaderMapPtr response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))}})};

  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      absl::StrCat("OauthHMAC=", encoded_token, cookie_tail_http_only));
  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      absl::StrCat("OauthExpires=", new_expires_, cookie_tail_http_only));

  // If opted-in, we also create a new Bearer cookie for the authorization token provided by the
  // auth server.
  if (config_->forwardBearerToken()) {
    response_headers->addReferenceKey(Http::Headers::get().SetCookie,
                                      absl::StrCat("BearerToken=", access_token_, cookie_tail));
  }

  response_headers->setLocation(state_);

  decoder_callbacks_->encodeHeaders(std::move(response_headers), true, REDIRECT_LOGGED_IN);
  config_->stats().oauth_success_.inc();
  decoder_callbacks_->continueDecoding();
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
