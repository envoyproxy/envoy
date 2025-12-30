#include "source/extensions/filters/http/oauth2/filter.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/common/base64.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/fmt.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/retry_policy_impl.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "jwt_verify_lib/jwt.h"
#include "jwt_verify_lib/status.h"
#include "openssl/rand.h"

using namespace std::chrono_literals;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

namespace {
Http::RegisterCustomInlineHeader<Http::CustomInlineHeaderRegistry::Type::RequestHeaders>
    authorization_handle(Http::CustomHeaders::get().Authorization);

constexpr const char* CookieDeleteFormatString =
    "{}=deleted; path={}; expires=Thu, 01 Jan 1970 00:00:00 GMT";
constexpr const char* CookieTailHttpOnlyFormatString = ";path={};Max-Age={};secure;HttpOnly{}";
constexpr const char* CookieDomainFormatString = ";domain={}";

constexpr const char* OIDCLogoutUrlFormatString =
    "{0}?id_token_hint={1}&client_id={2}&post_logout_redirect_uri={3}";

constexpr absl::string_view UnauthorizedBodyMessage = "OAuth flow failed.";

constexpr absl::string_view queryParamsError = "error";
constexpr absl::string_view queryParamsCode = "code";
constexpr absl::string_view queryParamsState = "state";
constexpr absl::string_view queryParamsRedirectUri = "redirect_uri";
constexpr absl::string_view queryParamsCodeChallenge = "code_challenge";
constexpr absl::string_view queryParamsCodeChallengeMethod = "code_challenge_method";

constexpr absl::string_view stateParamsUrl = "url";
constexpr absl::string_view stateParamsCsrfToken = "csrf_token";
constexpr absl::string_view stateParamsFlowId = "flow_id";

constexpr absl::string_view REDIRECT_RACE = "oauth.race_redirect";
constexpr absl::string_view REDIRECT_LOGGED_IN = "oauth.logged_in";
constexpr absl::string_view REDIRECT_FOR_CREDENTIALS = "oauth.missing_credentials";
constexpr absl::string_view SIGN_OUT = "oauth.sign_out";
constexpr absl::string_view DEFAULT_AUTH_SCOPE = "user";
constexpr absl::string_view OAUTH2_SCOPE_OPENID = "openid";

constexpr absl::string_view SameSiteLax = ";SameSite=Lax";
constexpr absl::string_view SameSiteStrict = ";SameSite=Strict";
constexpr absl::string_view SameSiteNone = ";SameSite=None";
constexpr absl::string_view HmacPayloadSeparator = "\n";
constexpr absl::string_view CookieSuffixDelimiter = ".";

constexpr int DEFAULT_CSRF_TOKEN_EXPIRES_IN = 600;
constexpr int DEFAULT_CODE_VERIFIER_TOKEN_EXPIRES_IN = 600;

template <class T>
std::vector<Http::HeaderUtility::HeaderDataPtr>
headerMatchers(const T& matcher_protos, Server::Configuration::CommonFactoryContext& context) {
  std::vector<Http::HeaderUtility::HeaderDataPtr> matchers;
  matchers.reserve(matcher_protos.size());

  for (const auto& proto : matcher_protos) {
    matchers.emplace_back(Http::HeaderUtility::createHeaderData(proto, context));
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
    result += "&resource=" + Http::Utility::PercentEncoding::urlEncode(resource);
  }
  return result;
}

// Sets the auth token as the Bearer token in the authorization header.
void setBearerToken(Http::RequestHeaderMap& headers, const std::string& token) {
  headers.setInline(authorization_handle.handle(), absl::StrCat("Bearer ", token));
}

std::string cookieNameWithSuffix(absl::string_view base_name, absl::string_view suffix) {
  return absl::StrCat(base_name, CookieSuffixDelimiter, suffix);
}

bool cookieNameMatchesBase(absl::string_view cookie_name, absl::string_view base_name) {
  // TODO(Huabing): Remove this once all supported releases understand suffixed names.
  if (cookie_name == base_name) {
    return true;
  }
  if (cookie_name.size() <= base_name.size() + CookieSuffixDelimiter.size()) {
    return false;
  }
  return cookie_name.starts_with(absl::StrCat(base_name, CookieSuffixDelimiter));
}

absl::optional<std::string> readCookieValueWithSuffix(const Http::RequestHeaderMap& headers,
                                                      absl::string_view base_name,
                                                      absl::string_view suffix) {
  const std::string suffixed_name = cookieNameWithSuffix(base_name, suffix);
  std::string value = Http::Utility::parseCookieValue(headers, suffixed_name);
  if (!value.empty()) {
    return value;
  }

  // Fall back to the legacy cookie name without the flow-specific suffix for backward
  // compatibility with older Envoy versions that do not append the suffix.
  // TODO(Huabing): Remove this once all supported releases understand suffixed names.
  value = Http::Utility::parseCookieValue(headers, std::string(base_name));
  if (!value.empty()) {
    return value;
  }
  return absl::nullopt;
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

// Helper function to get SameSite attribute string from proto enum.
std::string
getSameSiteString(envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite same_site) {
  switch (same_site) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
      CookieConfig_SameSite_STRICT:
    return std::string(SameSiteStrict);
  case envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
      CookieConfig_SameSite_LAX:
    return std::string(SameSiteLax);
  case envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
      CookieConfig_SameSite_NONE:
    return std::string(SameSiteNone);
  case envoy::extensions::filters::http::oauth2::v3::CookieConfig_SameSite::
      CookieConfig_SameSite_DISABLED:
    return EMPTY_STRING;
  }
  IS_ENVOY_BUG("unexpected same_site enum value");
  return EMPTY_STRING;
}

Http::Utility::QueryParamsMulti buildAutorizationQueryParams(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2Config& proto_config) {
  auto query_params =
      Http::Utility::QueryParamsMulti::parseQueryString(proto_config.authorization_endpoint());
  query_params.overwrite("client_id", proto_config.credentials().client_id());
  query_params.overwrite("response_type", "code");
  std::string scopes_list = absl::StrJoin(authScopesList(proto_config.auth_scopes()), " ");
  query_params.overwrite("scope", Http::Utility::PercentEncoding::urlEncode(scopes_list));
  return query_params;
}

std::string encodeHmacHexBase64(const std::vector<uint8_t>& secret, absl::string_view domain,
                                absl::string_view expires, absl::string_view token = "",
                                absl::string_view id_token = "",
                                absl::string_view refresh_token = "") {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  const auto hmac_payload =
      absl::StrJoin({domain, expires, token, id_token, refresh_token}, HmacPayloadSeparator);
  std::string encoded_hmac;
  absl::Base64Escape(Hex::encode(crypto_util.getSha256Hmac(secret, hmac_payload)), &encoded_hmac);
  return encoded_hmac;
}

// Generates a SHA256 HMAC from a secret and a message and returns the result as a base64 encoded
// string.
std::string generateHmacBase64(const std::vector<uint8_t>& secret, absl::string_view message) {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  std::vector<uint8_t> hmac_result = crypto_util.getSha256Hmac(secret, message);
  std::string hmac_string(hmac_result.begin(), hmac_result.end());
  std::string base64_encoded_hmac;
  absl::Base64Escape(hmac_string, &base64_encoded_hmac);
  return base64_encoded_hmac;
}

std::string encodeHmacBase64(const std::vector<uint8_t>& secret, absl::string_view domain,
                             absl::string_view expires, absl::string_view token = "",
                             absl::string_view id_token = "",
                             absl::string_view refresh_token = "") {
  std::string hmac_payload =
      absl::StrJoin({domain, expires, token, id_token, refresh_token}, HmacPayloadSeparator);
  return generateHmacBase64(secret, hmac_payload);
}

std::string encodeHmac(const std::vector<uint8_t>& secret, absl::string_view domain,
                       absl::string_view expires, absl::string_view token = "",
                       absl::string_view id_token = "", absl::string_view refresh_token = "") {
  return encodeHmacBase64(secret, domain, expires, token, id_token, refresh_token);
}

// Generates a CSRF token that can be used to prevent CSRF attacks.
// The token is in the format of <nonce>.<hmac(nonce)> recommended by
// https://cheatsheetseries.owasp.org/cheatsheets/Cross-Site_Request_Forgery_Prevention_Cheat_Sheet.html#signed-double-submit-cookie-recommended
std::string generateCsrfToken(absl::string_view hmac_secret, absl::string_view random_string) {
  std::vector<uint8_t> hmac_secret_vec(hmac_secret.begin(), hmac_secret.end());
  std::string hmac = generateHmacBase64(hmac_secret_vec, random_string);
  std::string csrf_token = fmt::format("{}.{}", random_string, hmac);
  return csrf_token;
}

// validate the csrf token hmac to prevent csrf token forgery
bool validateCsrfTokenHmac(const std::string& hmac_secret, const std::string& csrf_token) {
  size_t pos = csrf_token.find('.');
  if (pos == std::string::npos) {
    return false;
  }

  std::string token = std::string(csrf_token.substr(0, pos));
  std::string hmac = std::string(csrf_token.substr(pos + 1));
  std::vector<uint8_t> hmac_secret_vec(hmac_secret.begin(), hmac_secret.end());
  return generateHmacBase64(hmac_secret_vec, token) == hmac;
}

// Generates a PKCE code verifier with 32 octets of randomness.
// This follows recommendations in RFC 7636:
// https://datatracker.ietf.org/doc/html/rfc7636#section-7.1
std::string generateCodeVerifier(Random::RandomGenerator& random) {
  MemBlockBuilder<uint64_t> mem_block(4);
  // create 4 random uint64_t values to fill the buffer because RFC 7636 recommends 32 octets of
  // randomness.
  for (size_t i = 0; i < 4; i++) {
    mem_block.appendOne(random.random());
  }

  std::unique_ptr<uint64_t[]> data = mem_block.release();
  return Base64Url::encode(reinterpret_cast<char*>(data.get()), 4 * sizeof(uint64_t));
}

// Generates a PKCE code challenge from a code verifier.
std::string generateCodeChallenge(const std::string& code_verifier) {
  auto& crypto_util = Envoy::Common::Crypto::UtilitySingleton::get();
  std::vector<uint8_t> sha256_digest =
      crypto_util.getSha256Digest(Buffer::OwnedImpl(code_verifier));
  std::string sha256_string(sha256_digest.begin(), sha256_digest.end());
  return Base64Url::encode(sha256_string.data(), sha256_string.size());
}

/**
 * Encodes the state parameter for the OAuth2 flow.
 * The state parameter is a base64Url encoded JSON object containing the original request URL, a
 * CSRF token for CSRF protection, and the flow id used to store flow-specific cookies.
 */
std::string encodeState(absl::string_view original_request_url, const absl::string_view csrf_token,
                        absl::string_view flow_id) {
  std::string url_buffer;
  std::string csrf_buffer;
  std::string flow_id_buffer;
  absl::string_view sanitized_url = Json::sanitize(url_buffer, original_request_url);
  absl::string_view sanitized_csrf_token = Json::sanitize(csrf_buffer, csrf_token);
  absl::string_view sanitized_flow_id = Json::sanitize(flow_id_buffer, flow_id);
  std::string json = fmt::format(R"({{"url":"{}","csrf_token":"{}","flow_id":"{}"}})",
                                 sanitized_url, sanitized_csrf_token, sanitized_flow_id);
  return Base64Url::encode(json.data(), json.size());
}

/**
 * Encrypt a plaintext string using AES-256-CBC.
 */
std::string encrypt(const std::string& plaintext, const std::string& secret,
                    Random::RandomGenerator& random) {
  // Generate the key from the secret using SHA-256
  std::vector<unsigned char> key(SHA256_DIGEST_LENGTH); // AES-256 requires 256-bit (32 bytes) key
  SHA256(reinterpret_cast<const unsigned char*>(secret.c_str()), secret.size(), key.data());

  // Generate a random IV
  MemBlockBuilder<uint64_t> mem_block(4);
  // create 2 random uint64_t values to fill the buffer because AES-256-CBC requires 16 bytes IV
  for (size_t i = 0; i < 2; i++) {
    mem_block.appendOne(random.random());
  }

  std::unique_ptr<uint64_t[]> data = mem_block.release();
  const unsigned char* raw_data = reinterpret_cast<const unsigned char*>(data.get());

  // AES uses 16-byte IV
  std::vector<unsigned char> iv(16);
  iv.assign(raw_data, raw_data + 16);

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  RELEASE_ASSERT(ctx, "Failed to create context");

  std::vector<unsigned char> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
  int len = 0, ciphertext_len = 0;

  // Initialize encryption operation
  int result = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data());
  RELEASE_ASSERT(result == 1, "Encryption initialization failed");

  // Encrypt the plaintext
  result = EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                             reinterpret_cast<const unsigned char*>(plaintext.c_str()),
                             plaintext.size());
  RELEASE_ASSERT(result == 1, "Encryption update failed");

  ciphertext_len += len;

  // Finalize encryption
  result = EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
  RELEASE_ASSERT(result == 1, "Encryption finalization failed");

  ciphertext_len += len;

  EVP_CIPHER_CTX_free(ctx);

  // AES uses 16-byte IV
  ciphertext.resize(ciphertext_len);

  // Prepend the IV to the ciphertext
  std::vector<unsigned char> combined(iv.size() + ciphertext.size());
  std::copy(iv.begin(), iv.end(), combined.begin());
  std::copy(ciphertext.begin(), ciphertext.end(), combined.begin() + iv.size());

  // Base64Url encode the IV + ciphertext
  return Base64Url::encode(reinterpret_cast<const char*>(combined.data()), combined.size());
}

struct DecryptResult {
  std::string plaintext;
  absl::optional<std::string> error;
};

/**
 * Decrypt an AES-256-CBC encrypted string.
 */
DecryptResult decrypt(const std::string& encrypted, const std::string& secret) {
  // Decode the Base64Url-encoded input
  std::string decoded = Base64Url::decode(encrypted);
  std::vector<unsigned char> combined(decoded.begin(), decoded.end());

  if (combined.size() <= 16) {
    return {"", "Invalid encrypted data"};
  }

  // Extract the IV (first 16 bytes)
  std::vector<unsigned char> iv(combined.begin(), combined.begin() + 16);

  // Extract the ciphertext (remaining bytes)
  std::vector<unsigned char> ciphertext(combined.begin() + 16, combined.end());

  // Generate the key from the secret using SHA-256
  std::vector<unsigned char> key(SHA256_DIGEST_LENGTH);
  SHA256(reinterpret_cast<const unsigned char*>(secret.c_str()), secret.size(), key.data());

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  RELEASE_ASSERT(ctx, "Failed to create context");

  std::vector<unsigned char> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
  int len = 0, plaintext_len = 0;

  // Initialize decryption operation
  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {"", "failed to initialize decryption"};
  }

  // Decrypt the ciphertext
  if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size()) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {"", "failed to decrypt data"};
  }
  plaintext_len += len;

  // Finalize decryption
  if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
    EVP_CIPHER_CTX_free(ctx);
    return {"", "failed to finalize decryption"};
  }

  plaintext_len += len;

  EVP_CIPHER_CTX_free(ctx);

  // Resize to actual plaintext length
  plaintext.resize(plaintext_len);

  return {std::string(plaintext.begin(), plaintext.end()), std::nullopt};
}

} // namespace

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2Config& proto_config,
    Server::Configuration::CommonFactoryContext& context,
    std::shared_ptr<SecretReader> secret_reader, Stats::Scope& scope,
    const std::string& stats_prefix)
    : oauth_token_endpoint_(proto_config.token_endpoint()),
      authorization_endpoint_(proto_config.authorization_endpoint()),
      end_session_endpoint_(proto_config.end_session_endpoint()),
      authorization_query_params_(buildAutorizationQueryParams(proto_config)),
      client_id_(proto_config.credentials().client_id()),
      redirect_uri_(proto_config.redirect_uri()),
      redirect_matcher_(proto_config.redirect_path_matcher(), context),
      signout_path_(proto_config.signout_path(), context), secret_reader_(secret_reader),
      stats_(FilterConfig::generateStats(stats_prefix, proto_config.stat_prefix(), scope)),
      encoded_resource_query_params_(encodeResourceList(proto_config.resources())),
      pass_through_header_matchers_(headerMatchers(proto_config.pass_through_matcher(), context)),
      deny_redirect_header_matchers_(headerMatchers(proto_config.deny_redirect_matcher(), context)),
      cookie_names_(proto_config.credentials().cookie_names()),
      cookie_domain_(proto_config.credentials().cookie_domain()),
      auth_type_(getAuthType(proto_config.auth_type())),
      default_expires_in_(PROTOBUF_GET_SECONDS_OR_DEFAULT(proto_config, default_expires_in, 0)),
      default_refresh_token_expires_in_(
          PROTOBUF_GET_SECONDS_OR_DEFAULT(proto_config, default_refresh_token_expires_in, 604800)),
      csrf_token_expires_in_(PROTOBUF_GET_SECONDS_OR_DEFAULT(proto_config, csrf_token_expires_in,
                                                             DEFAULT_CSRF_TOKEN_EXPIRES_IN)),
      code_verifier_token_expires_in_(PROTOBUF_GET_SECONDS_OR_DEFAULT(
          proto_config, code_verifier_token_expires_in, DEFAULT_CODE_VERIFIER_TOKEN_EXPIRES_IN)),
      forward_bearer_token_(proto_config.forward_bearer_token()),
      preserve_authorization_header_(proto_config.preserve_authorization_header()),
      use_refresh_token_(FilterConfig::shouldUseRefreshToken(proto_config)),
      disable_id_token_set_cookie_(proto_config.disable_id_token_set_cookie()),
      disable_access_token_set_cookie_(proto_config.disable_access_token_set_cookie()),
      disable_refresh_token_set_cookie_(proto_config.disable_refresh_token_set_cookie()),
      disable_token_encryption_(proto_config.disable_token_encryption()),
      bearer_token_cookie_settings_(
          (proto_config.has_cookie_configs() &&
           proto_config.cookie_configs().has_bearer_token_cookie_config())
              ? CookieSettings(proto_config.cookie_configs().bearer_token_cookie_config())
              : CookieSettings()),
      hmac_cookie_settings_(
          (proto_config.has_cookie_configs() &&
           proto_config.cookie_configs().has_oauth_hmac_cookie_config())
              ? CookieSettings(proto_config.cookie_configs().oauth_hmac_cookie_config())
              : CookieSettings()),
      expires_cookie_settings_(
          (proto_config.has_cookie_configs() &&
           proto_config.cookie_configs().has_oauth_expires_cookie_config())
              ? CookieSettings(proto_config.cookie_configs().oauth_expires_cookie_config())
              : CookieSettings()),
      id_token_cookie_settings_(
          (proto_config.has_cookie_configs() &&
           proto_config.cookie_configs().has_id_token_cookie_config())
              ? CookieSettings(proto_config.cookie_configs().id_token_cookie_config())
              : CookieSettings()),
      refresh_token_cookie_settings_(
          (proto_config.has_cookie_configs() &&
           proto_config.cookie_configs().has_refresh_token_cookie_config())
              ? CookieSettings(proto_config.cookie_configs().refresh_token_cookie_config())
              : CookieSettings()),
      nonce_cookie_settings_(
          (proto_config.has_cookie_configs() &&
           proto_config.cookie_configs().has_oauth_nonce_cookie_config())
              ? CookieSettings(proto_config.cookie_configs().oauth_nonce_cookie_config())
              : CookieSettings()),
      code_verifier_cookie_settings_(
          (proto_config.has_cookie_configs() &&
           proto_config.cookie_configs().has_code_verifier_cookie_config())
              ? CookieSettings(proto_config.cookie_configs().code_verifier_cookie_config())
              : CookieSettings()) {
  if (!context.clusterManager().hasCluster(oauth_token_endpoint_.cluster())) {
    // This is not necessarily a configuration error — sometimes cluster is sent later than the
    // listener in the xDS stream.
    ENVOY_LOG(warn, "OAuth2 filter: unknown cluster '{}' in config. ",
              oauth_token_endpoint_.cluster());
  }
  if (!authorization_endpoint_url_.initialize(authorization_endpoint_,
                                              /*is_connect_request=*/false)) {
    throw EnvoyException(
        fmt::format("OAuth2 filter: invalid authorization endpoint URL '{}' in config.",
                    authorization_endpoint_));
  }
  if (!end_session_endpoint_.empty()) {
    bool is_oidc = false;
    for (const auto& scope : proto_config.auth_scopes()) {
      if (scope == OAUTH2_SCOPE_OPENID) {
        is_oidc = true;
        break;
      }
    }
    if (!is_oidc) {
      throw EnvoyException(
          "OAuth2 filter: end session endpoint is only supported for OpenID Connect.");
    }
  }

  if (proto_config.has_retry_policy()) {
    auto retry_policy = Http::Utility::convertCoreToRouteRetryPolicy(
        proto_config.retry_policy(), "5xx,gateway-error,connect-failure,reset");
    // Use the null validation visitor for the backward compatibility. The proto should already
    // been validated during the config load.
    auto parsed_policy_or_error = Router::RetryPolicyImpl::create(
        retry_policy, ProtobufMessage::getNullValidationVisitor(), context);
    THROW_IF_NOT_OK_REF(parsed_policy_or_error.status());
    retry_policy_ = std::move(parsed_policy_or_error.value());
  }
}

FilterStats FilterConfig::generateStats(const std::string& prefix,
                                        const std::string& filter_stats_prefix,
                                        Stats::Scope& scope) {
  const std::string final_prefix = absl::StrCat(prefix, filter_stats_prefix);
  return {ALL_OAUTH_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

bool FilterConfig::shouldUseRefreshToken(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2Config& proto_config) const {
  return PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, use_refresh_token, true);
}

void OAuth2CookieValidator::setParams(const Http::RequestHeaderMap& headers,
                                      const std::string& secret) {
  const auto& cookies = Http::Utility::parseCookies(headers, [this](absl::string_view key) -> bool {
    return key == cookie_names_.oauth_expires_ || key == cookie_names_.bearer_token_ ||
           key == cookie_names_.oauth_hmac_ || key == cookie_names_.id_token_ ||
           key == cookie_names_.refresh_token_;
  });

  expires_ = findValue(cookies, cookie_names_.oauth_expires_);
  access_token_ = findValue(cookies, cookie_names_.bearer_token_);
  id_token_ = findValue(cookies, cookie_names_.id_token_);
  refresh_token_ = findValue(cookies, cookie_names_.refresh_token_);
  hmac_ = findValue(cookies, cookie_names_.oauth_hmac_);
  host_ = headers.Host()->value().getStringView();

  secret_.assign(secret.begin(), secret.end());
}

bool OAuth2CookieValidator::canUpdateTokenByRefreshToken() const { return !refresh_token_.empty(); }

bool OAuth2CookieValidator::hmacIsValid() const {
  absl::string_view cookie_domain = host_;
  if (!cookie_domain_.empty()) {
    cookie_domain = cookie_domain_;
  }
  return ((encodeHmacBase64(secret_, cookie_domain, expires_, access_token_, id_token_,
                            refresh_token_) == hmac_) ||
          (encodeHmacHexBase64(secret_, cookie_domain, expires_, access_token_, id_token_,
                               refresh_token_) == hmac_));
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
                           std::unique_ptr<OAuth2Client>&& oauth_client, TimeSource& time_source,
                           Random::RandomGenerator& random)
    : validator_(std::make_shared<OAuth2CookieValidator>(time_source, config->cookieNames(),
                                                         config->cookieDomain())),
      oauth_client_(std::move(oauth_client)), config_(std::move(config)), time_source_(time_source),
      random_(random) {

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
  // Skip Filter and continue chain if a Passthrough header is matching.
  // Only increment counters here; do not modify request headers, as there may be
  // other instances of this filter configured that still need to process the request.
  for (const auto& matcher : config_->passThroughMatchers()) {
    if (matcher->matchesHeaders(headers)) {
      config_->stats().oauth_passthrough_.inc();
      return Http::FilterHeadersStatus::Continue;
    }
  }

  // Decrypt the OAuth tokens and update the corresponding cookies in the request headers
  // before forwarding the request upstream. This step must occur early to ensure that
  // other parts of the filter can access the decrypted tokens—for example, to calculate
  // the HMAC for the cookies.
  decryptAndUpdateOAuthTokenCookies(headers);

  // Only sanitize the Authorization header if preserveAuthorizationHeader is false
  if (!config_->preserveAuthorizationHeader()) {
    // Sanitize the Authorization header, since we have no way to validate its content. Also,
    // if token forwarding is enabled, this header will be set based on what is on the HMAC cookie
    // before forwarding the request upstream.
    headers.removeInline(authorization_handle.handle());
  }

  // The following 2 headers are guaranteed for regular requests. The asserts are helpful when
  // writing test code to not forget these important variables in mock requests
  const Http::HeaderEntry* host_header = headers.Host();
  ASSERT(host_header != nullptr);
  host_ = host_header->value().getStringView();

  const Http::HeaderEntry* path_header = headers.Path();
  ASSERT(path_header != nullptr);
  const absl::string_view path_str = path_header->value().getStringView();
  const bool redirect_from_auth_server = config_->redirectPathMatcher().match(path_str);

  // Save the request headers for later modification if needed.
  request_headers_ = &headers;

  // We should check if this is a sign out request.
  if (config_->signoutPath().match(path_header->value().getStringView())) {
    return signOutUser(headers);
  }

  // The user has already logged in and not expired.
  const bool logged_in = canSkipOAuth(headers);
  if (logged_in) {
    // Update the path header with the query string parameters after a successful OAuth login.
    // This is necessary if a website requests multiple resources which get redirected to the
    // auth server. A cached login on the authorization server side will set cookies
    // correctly but cause a race condition on future requests that have their location set
    // to the callback path.
    if (redirect_from_auth_server) {
      // Even though we're already logged in and don't technically need to validate the presence
      // of the auth code, we still perform the validation to ensure consistency and reuse the
      // validateOAuthCallback method. This is acceptable because the auth code is always present
      // in the query string of the callback path according to the OAuth2 spec.
      // More information can be found here:
      // https://datatracker.ietf.org/doc/html/rfc6749#section-4.1.2
      const CallbackValidationResult result = validateOAuthCallback(headers, path_str);
      flow_id_ = result.flow_id_;
      if (!result.is_valid_) {
        sendUnauthorizedResponse(result.error_details_);
        return Http::FilterHeadersStatus::StopIteration;
      }

      // Return 401 unauthorized if the original request URL in the state matches the redirect
      // config to avoid infinite redirect loops.
      Http::Utility::Url original_request_url;
      original_request_url.initialize(result.original_request_url_, false);
      if (config_->redirectPathMatcher().match(original_request_url.pathAndQueryParams())) {
        sendUnauthorizedResponse(
            fmt::format("State url query params matches the redirect path matcher: {}",
                        original_request_url.pathAndQueryParams()));
        return Http::FilterHeadersStatus::StopIteration;
      }

      // Since the user is already logged in, we don't need to exchange the auth code for tokens.
      // Instead, we redirect the user back to the original request URL.
      Http::ResponseHeaderMapPtr response_headers{
          Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
              {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))},
               {Http::Headers::get().Location, result.original_request_url_}})};
      addFlowCookieDeletionHeaders(*response_headers, flow_id_);
      decoder_callbacks_->encodeHeaders(std::move(response_headers), true, REDIRECT_RACE);
      return Http::FilterHeadersStatus::StopIteration;
    }

    // User is already login, and this is a resource request.
    // Remove OAuth flow cookies to prevent them from being sent upstream.
    removeOAuthFlowCookies(headers);
    // Continue on with the filter stack.
    return Http::FilterHeadersStatus::Continue;
  }

  // If this isn't the callback URI, redirect to the auth server to start the OAuth flow.
  //
  // The following conditional could be replaced with a regex pattern-match,
  // if we're concerned about strict matching against the callback path.
  if (!redirect_from_auth_server) {

    // Check if we can update the access token via a refresh token.
    if (config_->useRefreshToken() && validator_->canUpdateTokenByRefreshToken()) {

      ENVOY_LOG(debug, "Trying to update the access token using the refresh token");

      // try to update access token by refresh token
      oauth_client_->asyncRefreshAccessToken(validator_->refreshToken(), config_->clientId(),
                                             config_->clientSecret(), config_->authType());
      // pause while we await the next step from the OAuth server
      return Http::FilterHeadersStatus::StopAllIterationAndWatermark;
    }

    if (canRedirectToOAuthServer(headers)) {
      ENVOY_LOG(debug, "redirecting to OAuth server", path_str);
      redirectToOAuthServer(headers);
      return Http::FilterHeadersStatus::StopIteration;
    } else {
      sendUnauthorizedResponse(fmt::format(
          "Unauthorized, and redirecting to OAuth server is not allowed: {}", path_str));
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  // At this point, we *are* on /_oauth. We believe this request comes from the authorization
  // server and we expect the query strings to contain the information required to get the access
  // token.
  const CallbackValidationResult result = validateOAuthCallback(headers, path_str);
  flow_id_ = result.flow_id_;
  if (!result.is_valid_) {
    sendUnauthorizedResponse(result.error_details_);
    return Http::FilterHeadersStatus::StopIteration;
  }

  original_request_url_ = result.original_request_url_;
  auth_code_ = result.auth_code_;
  Formatter::FormatterPtr formatter = THROW_OR_RETURN_VALUE(
      Formatter::FormatterImpl::create(config_->redirectUri()), Formatter::FormatterPtr);
  const auto redirect_uri = formatter->format({&headers}, decoder_callbacks_->streamInfo());

  absl::optional<std::string> encrypted_code_verifier =
      readCookieValueWithSuffix(headers, config_->cookieNames().code_verifier_, result.flow_id_);
  if (!encrypted_code_verifier.has_value()) {
    sendUnauthorizedResponse("Code verifier cookie is missing in the request");
    return Http::FilterHeadersStatus::StopIteration;
  }

  DecryptResult decrypt_result = decrypt(encrypted_code_verifier.value(), config_->hmacSecret());
  if (decrypt_result.error.has_value()) {
    sendUnauthorizedResponse(fmt::format("Failed to decrypt code verifier: {}, error: {}",
                                         encrypted_code_verifier.value(),
                                         decrypt_result.error.value()));
    return Http::FilterHeadersStatus::StopIteration;
  }
  std::string code_verifier = decrypt_result.plaintext;

  oauth_client_->asyncGetAccessToken(auth_code_, config_->clientId(), config_->clientSecret(),
                                     redirect_uri, code_verifier, config_->authType());

  // pause while we await the next step from the OAuth server
  return Http::FilterHeadersStatus::StopAllIterationAndBuffer;
}

Http::FilterHeadersStatus OAuth2Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  if (was_refresh_token_flow_) {
    setOAuthResponseCookies(headers, getEncodedToken());
    was_refresh_token_flow_ = false;
  }

  return Http::FilterHeadersStatus::Continue;
}

// Defines a sequence of checks determining whether we should initiate a new OAuth flow or skip to
// the next filter in the chain.
bool OAuth2Filter::canSkipOAuth(Http::RequestHeaderMap& headers) const {
  // We can skip OAuth if the supplied HMAC cookie is valid. Apply the OAuth details as headers
  // if we successfully validate the cookie.
  validator_->setParams(headers, config_->hmacSecret());
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

// Decrypt the OAuth tokens and updates the OAuth tokens in the request cookies before forwarding
// the request upstream.
void OAuth2Filter::decryptAndUpdateOAuthTokenCookies(Http::RequestHeaderMap& headers) const {
  if (config_->disableTokenEncryption()) {
    return;
  }

  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth2_encrypt_tokens")) {
    return;
  }

  absl::flat_hash_map<std::string, std::string> cookies = Http::Utility::parseCookies(headers);
  if (cookies.empty()) {
    return;
  }

  const CookieNames& cookie_names = config_->cookieNames();

  const std::string encrypted_access_token = findValue(cookies, cookie_names.bearer_token_);
  const std::string encrypted_id_token = findValue(cookies, cookie_names.id_token_);
  const std::string encrypted_refresh_token = findValue(cookies, cookie_names.refresh_token_);

  if (!encrypted_access_token.empty()) {
    cookies.insert_or_assign(cookie_names.bearer_token_, decryptToken(encrypted_access_token));
  }

  if (!encrypted_id_token.empty()) {
    cookies.insert_or_assign(cookie_names.id_token_, decryptToken(encrypted_id_token));
  }

  if (!encrypted_refresh_token.empty()) {
    cookies.insert_or_assign(cookie_names.refresh_token_, decryptToken(encrypted_refresh_token));
  }

  if (!encrypted_access_token.empty() || !encrypted_id_token.empty() ||
      !encrypted_refresh_token.empty()) {
    std::string new_cookies(absl::StrJoin(cookies, "; ", absl::PairFormatter("=")));
    headers.setReferenceKey(Http::Headers::get().Cookie, new_cookies);
  }
}

std::string OAuth2Filter::encryptToken(const std::string& token) const {
  if (config_->disableTokenEncryption()) {
    return token;
  }

  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth2_encrypt_tokens")) {
    return encrypt(token, config_->hmacSecret(), random_);
  }
  return token;
}

std::string OAuth2Filter::decryptToken(const std::string& encrypted_token) const {
  if (encrypted_token.empty()) {
    return EMPTY_STRING;
  }

  DecryptResult decrypt_result = decrypt(encrypted_token, config_->hmacSecret());
  if (decrypt_result.error.has_value()) {
    ENVOY_LOG(error, "failed to decrypt token: {}, error: {}", encrypted_token,
              decrypt_result.error.value());
    // There are two cases:
    // 1. The token is a legacy unencrypted token.
    // In this case, we return the token as-is to allow the request to proceed.
    // 2. The token is encrypted, but the decryption failed due to the HMAC secret is changed.
    // In this case, we return the original encrypted token, the HMAC validation will fail
    // and the user will be redirected to the OAuth server for re-authentication.
    return encrypted_token;
  }
  return decrypt_result.plaintext;
}

bool OAuth2Filter::canRedirectToOAuthServer(Http::RequestHeaderMap& headers) const {
  for (const auto& matcher : config_->denyRedirectMatchers()) {
    if (matcher->matchesHeaders(headers)) {
      ENVOY_LOG(debug, "redirect is denied for this request");
      return false;
    }
  }
  return true;
}

void OAuth2Filter::redirectToOAuthServer(Http::RequestHeaderMap& headers) {
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
  const std::string original_url = absl::StrCat(base_path, headers.Path()->value().getStringView());

  const CookieNames& cookie_names = config_->cookieNames();

  // Generate a random string to use as the unique id for this OAuth flow.
  const std::string random_string = Hex::uint64ToHex(random_.random());
  absl::string_view flow_id = random_string;

  // Generate a CSRF token to prevent CSRF attacks.
  const std::string csrf_token = generateCsrfToken(config_->hmacSecret(), random_string);
  const std::string csrf_expires = std::to_string(config_->getCsrfTokenExpiresIn().count());
  const std::string csrf_cookie_tail =
      buildCookieTail(config_->nonceCookieSettings(), csrf_expires);
  // Use the flow id to create a unique cookie name for this OAuth flow.
  // This allows multiple concurrent OAuth flows to be handled correctly.
  const std::string csrf_cookie_name = cookieNameWithSuffix(cookie_names.oauth_nonce_, flow_id);
  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      absl::StrCat(csrf_cookie_name, "=", csrf_token, csrf_cookie_tail));
  // Also set the legacy cookie without the flow id suffix for backward compatibility with Envoy
  // versions that do not support per-flow cookie names.
  // TODO(Huabing): Drop the legacy Set-Cookie once all supported releases understand suffixed
  // names.
  response_headers->addReferenceKey(
      Http::Headers::get().SetCookie,
      absl::StrCat(cookie_names.oauth_nonce_, "=", csrf_token, csrf_cookie_tail));

  // Encode the state parameter for the OAuth flow.
  // The flow id is included in the state to allow retrieval of flow-specific cookies later.
  const std::string state = encodeState(original_url, csrf_token, flow_id);
  auto query_params = config_->authorizationQueryParams();
  query_params.overwrite(queryParamsState, state);

  Formatter::FormatterPtr formatter = THROW_OR_RETURN_VALUE(
      Formatter::FormatterImpl::create(config_->redirectUri()), Formatter::FormatterPtr);
  const auto redirect_uri = formatter->format({&headers}, decoder_callbacks_->streamInfo());
  const std::string escaped_redirect_uri = Http::Utility::PercentEncoding::urlEncode(redirect_uri);
  query_params.overwrite(queryParamsRedirectUri, escaped_redirect_uri);

  // Generate a PKCE code verifier and challenge for the OAuth flow.
  const std::string code_verifier = generateCodeVerifier(random_);

  const std::chrono::seconds code_verifier_token_expires_in =
      config_->getCodeVerifierTokenExpiresIn();
  const std::string code_verifier_expire_in =
      std::to_string(code_verifier_token_expires_in.count());
  const std::string code_verifier_cookie_tail =
      buildCookieTail(config_->codeVerifierCookieSettings(), code_verifier_expire_in);
  // Use the flow id to create a unique cookie name for this OAuth flow.
  // This allows multiple concurrent OAuth flows to be handled correctly.
  const std::string encrypted_code_verifier =
      encrypt(code_verifier, config_->hmacSecret(), random_);
  const std::string code_verifier_cookie_name =
      cookieNameWithSuffix(cookie_names.code_verifier_, flow_id);
  response_headers->addReferenceKey(Http::Headers::get().SetCookie,
                                    absl::StrCat(code_verifier_cookie_name, "=",
                                                 encrypted_code_verifier,
                                                 code_verifier_cookie_tail));

  // Also set the legacy cookie without the flow id suffix for backward compatibility with Envoy
  // TODO(Huabing): Remove the extra legacy cookie once all supported releases understand suffixed
  // names.
  response_headers->addReferenceKey(Http::Headers::get().SetCookie,
                                    absl::StrCat(cookie_names.code_verifier_, "=",
                                                 encrypted_code_verifier,
                                                 code_verifier_cookie_tail));

  const std::string code_challenge = generateCodeChallenge(code_verifier);
  query_params.overwrite(queryParamsCodeChallenge, code_challenge);
  query_params.overwrite(queryParamsCodeChallengeMethod, "S256");

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
Http::FilterHeadersStatus OAuth2Filter::signOutUser(const Http::RequestHeaderMap& headers) const {
  Http::ResponseHeaderMapPtr response_headers{Http::createHeaderMap<Http::ResponseHeaderMapImpl>(
      {{Http::Headers::get().Status, std::to_string(enumToInt(Http::Code::Found))}})};
  const CookieNames& cookie_names = config_->cookieNames();

  // Map cookie names to their respective paths from configuration.
  std::vector<std::pair<absl::string_view, absl::string_view>> cookies_to_delete{
      {cookie_names.oauth_hmac_, config_->hmacCookieSettings().path_},
      {cookie_names.bearer_token_, config_->bearerTokenCookieSettings().path_},
      {cookie_names.id_token_, config_->idTokenCookieSettings().path_},
      {cookie_names.refresh_token_, config_->refreshTokenCookieSettings().path_},

  };

  absl::flat_hash_map<std::string, std::string> request_cookies =
      Http::Utility::parseCookies(headers);

  // Delete any flow-specific cookies that may exist.
  for (const auto& cookie : request_cookies) {
    if (cookieNameMatchesBase(cookie.first, cookie_names.oauth_nonce_)) {
      cookies_to_delete.emplace_back(cookie.first, config_->nonceCookieSettings().path_);
    } else if (cookieNameMatchesBase(cookie.first, cookie_names.code_verifier_)) {
      cookies_to_delete.emplace_back(cookie.first, config_->codeVerifierCookieSettings().path_);
    }
  }

  std::string cookie_domain;
  if (!config_->cookieDomain().empty()) {
    cookie_domain = fmt::format(CookieDomainFormatString, config_->cookieDomain());
  }

  for (const auto& [cookie_name, cookie_path] : cookies_to_delete) {
    // Cookie names prefixed with "__Secure-" or "__Host-" are special. They MUST be set with the
    // Secure attribute so that the browser handles their deletion properly.
    const bool add_secure_attr =
        cookie_name.starts_with("__Secure-") || cookie_name.starts_with("__Host-");
    const absl::string_view maybe_secure_attr = add_secure_attr ? "; Secure" : "";

    response_headers->addReferenceKey(
        Http::Headers::get().SetCookie,
        absl::StrCat(fmt::format(CookieDeleteFormatString, cookie_name, cookie_path), cookie_domain,
                     maybe_secure_attr));
  }

  const std::string post_logout_redirect_url =
      absl::StrCat(headers.getSchemeValue(), "://", host_, "/");
  // If the end session endpoint is set, redirect to it to log out the user from the OpenID
  // provider.
  if (!config_->endSessionEndpoint().empty()) {
    const std::string id_token =
        Http::Utility::parseCookieValue(headers, config_->cookieNames().id_token_);
    const std::string oidc_logout_url = fmt::format(
        OIDCLogoutUrlFormatString, config_->endSessionEndpoint(), id_token, config_->clientId(),
        Http::Utility::PercentEncoding::encode(post_logout_redirect_url, ":/=&?"));
    response_headers->setLocation(oidc_logout_url);
  } else {
    response_headers->setLocation(post_logout_redirect_url);
  }

  decoder_callbacks_->encodeHeaders(std::move(response_headers), true, SIGN_OUT);

  return Http::FilterHeadersStatus::StopIteration;
}

// Called after fetching access/refresh tokens.
void OAuth2Filter::updateTokens(const std::string& access_token, const std::string& id_token,
                                const std::string& refresh_token, std::chrono::seconds expires_in) {
  if (!config_->disableAccessTokenSetCookie()) {
    // Preventing this here excludes all other Access Token functionality
    // * setting the cookie
    // * omitting from HMAC computation (for setting, not for validating)
    access_token_ = access_token;
  }
  if (!config_->disableIdTokenSetCookie()) {
    // Preventing this here excludes all other ID Token functionality
    // * setting the cookie
    // * omitting from HMAC computation (for setting, not for validating)
    id_token_ = id_token;
  }
  // Only set the refresh token if use_refresh_token_ is enabled and it's not disabled by config
  if (config_->useRefreshToken() && !config_->disableRefreshTokenSetCookie()) {
    // Preventing this here excludes all other Refresh Token functionality
    // * setting the cookie
    // * omitting from HMAC computation (for setting, not for validating)
    refresh_token_ = refresh_token;
  } else {
    refresh_token_ = "";
  }

  expires_in_ = std::to_string(expires_in.count());
  expires_refresh_token_in_ = getExpiresTimeForRefreshToken(refresh_token, expires_in);
  expires_id_token_in_ = getExpiresTimeForIdToken(id_token, expires_in);

  const auto new_epoch = time_source_.systemTime() + expires_in;
  new_expires_ = std::to_string(
      std::chrono::duration_cast<std::chrono::seconds>(new_epoch.time_since_epoch()).count());
}

std::string OAuth2Filter::getEncodedToken() const {
  auto token_secret = config_->hmacSecret();
  std::vector<uint8_t> token_secret_vec(token_secret.begin(), token_secret.end());
  std::string encoded_token;

  absl::string_view domain = host_;
  if (!config_->cookieDomain().empty()) {
    domain = config_->cookieDomain();
  }

  encoded_token =
      encodeHmac(token_secret_vec, domain, new_expires_, access_token_, id_token_, refresh_token_);

  return encoded_token;
}

std::string
OAuth2Filter::getExpiresTimeForRefreshToken(const std::string& refresh_token,
                                            const std::chrono::seconds& expires_in) const {
  if (config_->useRefreshToken()) {
    ::google::jwt_verify::Jwt jwt;
    if (jwt.parseFromString(refresh_token) == ::google::jwt_verify::Status::Ok && jwt.exp_ != 0) {
      const std::chrono::seconds expiration_from_jwt = std::chrono::seconds{jwt.exp_};
      const std::chrono::seconds now =
          std::chrono::time_point_cast<std::chrono::seconds>(time_source_.systemTime())
              .time_since_epoch();

      if (now < expiration_from_jwt) {
        const auto expiration_epoch = expiration_from_jwt - now;
        return std::to_string(expiration_epoch.count());
      } else {
        ENVOY_LOG(debug, "The expiration time in the refresh token is less than the current time");
        return "0";
      }
    }
    ENVOY_LOG(debug, "The refresh token is not a JWT or exp claim is omitted. The lifetime of the "
                     "refresh token will be taken from filter configuration");
    const std::chrono::seconds default_refresh_token_expires_in =
        config_->defaultRefreshTokenExpiresIn();
    return std::to_string(default_refresh_token_expires_in.count());
  }
  return std::to_string(expires_in.count());
}

std::string OAuth2Filter::getExpiresTimeForIdToken(const std::string& id_token,
                                                   const std::chrono::seconds& expires_in) const {
  if (!id_token.empty()) {
    ::google::jwt_verify::Jwt jwt;
    if (jwt.parseFromString(id_token) == ::google::jwt_verify::Status::Ok && jwt.exp_ != 0) {
      const std::chrono::seconds expiration_from_jwt = std::chrono::seconds{jwt.exp_};
      const std::chrono::seconds now =
          std::chrono::time_point_cast<std::chrono::seconds>(time_source_.systemTime())
              .time_since_epoch();

      if (now < expiration_from_jwt) {
        const auto expiration_epoch = expiration_from_jwt - now;
        return std::to_string(expiration_epoch.count());
      } else {
        ENVOY_LOG(debug, "The expiration time in the id token is less than the current time");
        return "0";
      }
    }
    ENVOY_LOG(debug, "The id token is not a JWT or exp claim is omitted, even though it is "
                     "required by the OpenID Connect 1.0 specification. "
                     "The lifetime of the id token will be aligned with the access token");
    return std::to_string(expires_in.count());
  }
  return std::to_string(expires_in.count());
}

std::string OAuth2Filter::buildCookieTail(const FilterConfig::CookieSettings& settings,
                                          absl::string_view expires_time) const {
  std::string cookie_tail = fmt::format(CookieTailHttpOnlyFormatString, settings.path_,
                                        expires_time, getSameSiteString(settings.same_site_));
  if (!config_->cookieDomain().empty()) {
    cookie_tail =
        absl::StrCat(fmt::format(CookieDomainFormatString, config_->cookieDomain()), cookie_tail);
  }
  return cookie_tail;
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

  setOAuthResponseCookies(*response_headers, getEncodedToken());
  // Delete the csrf and code_verifier cookies after a successful OAuth flow.
  // These cookies are no longer needed and should be deleted to prevent bloating the requests.
  addFlowCookieDeletionHeaders(*response_headers, flow_id_);
  response_headers->setLocation(original_request_url_);

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

  // TODO(Huabing): remove oauth_expires_ cookie after
  // "envoy.reloadable_features.oauth2_cleanup_cookies" runtime flag is removed.
  cookies.insert_or_assign(cookie_names.oauth_expires_, new_expires_);

  if (!access_token_.empty()) {
    cookies.insert_or_assign(cookie_names.bearer_token_, access_token_);
  }
  if (!id_token_.empty()) {
    cookies.insert_or_assign(cookie_names.id_token_, id_token_);
  }

  // TODO(Huabing): remove refresh_token_ cookie after
  // "envoy.reloadable_features.oauth2_cleanup_cookies" runtime flag is removed.
  if (!refresh_token_.empty()) {
    cookies.insert_or_assign(cookie_names.refresh_token_, refresh_token_);
  } else if (cookies.contains(cookie_names.refresh_token_)) {
    // If we actually went through the refresh token flow, but we didn't get a new refresh token,
    // we want to still ensure that the old one is set if it was sent in a cookie
    refresh_token_ = findValue(cookies, cookie_names.refresh_token_);
  }

  // TODO(Huabing): remove oauth_hmac_ cookie after
  // "envoy.reloadable_features.oauth2_cleanup_cookies" runtime flag is removed.
  cookies.insert_or_assign(cookie_names.oauth_hmac_, getEncodedToken());

  std::string new_cookies(absl::StrJoin(cookies, "; ", absl::PairFormatter("=")));
  request_headers_->setReferenceKey(Http::Headers::get().Cookie, new_cookies);
  if (config_->forwardBearerToken() && !access_token_.empty()) {
    setBearerToken(*request_headers_, access_token_);
  }

  was_refresh_token_flow_ = true;

  config_->stats().oauth_refreshtoken_success_.inc();
  config_->stats().oauth_success_.inc();

  // Remove OAuth flow cookies to prevent them from being sent upstream.
  removeOAuthFlowCookies(*request_headers_);
  decoder_callbacks_->continueDecoding();
}

void OAuth2Filter::onRefreshAccessTokenFailure() {
  config_->stats().oauth_refreshtoken_failure_.inc();
  // We failed to get an access token via the refresh token, so send the user to the oauth
  // endpoint.
  if (canRedirectToOAuthServer(*request_headers_)) {
    redirectToOAuthServer(*request_headers_);
  } else {
    sendUnauthorizedResponse(
        "Failed to refresh the access token, and redirecting to OAuth server is not allowed");
  }
}

void OAuth2Filter::setOAuthResponseCookies(Http::ResponseHeaderMap& headers,
                                           const std::string& encoded_token) const {
  // We use HTTP Only cookies.
  const CookieNames& cookie_names = config_->cookieNames();

  // Set the cookies in the response headers.
  headers.addReferenceKey(
      Http::Headers::get().SetCookie,
      absl::StrCat(cookie_names.oauth_hmac_, "=", encoded_token,
                   buildCookieTail(config_->hmacCookieSettings(), expires_in_)));

  headers.addReferenceKey(
      Http::Headers::get().SetCookie,
      absl::StrCat(cookie_names.oauth_expires_, "=", new_expires_,
                   buildCookieTail(config_->expiresCookieSettings(), expires_in_)));

  absl::flat_hash_map<std::string, std::string> request_cookies =
      Http::Utility::parseCookies(*request_headers_);
  std::string cookie_domain;
  if (!config_->cookieDomain().empty()) {
    cookie_domain = fmt::format(CookieDomainFormatString, config_->cookieDomain());
  }

  if (!access_token_.empty()) {
    headers.addReferenceKey(
        Http::Headers::get().SetCookie,
        absl::StrCat(cookie_names.bearer_token_, "=", encryptToken(access_token_),
                     buildCookieTail(config_->bearerTokenCookieSettings(), expires_in_)));
  } else if (request_cookies.contains(cookie_names.bearer_token_)) {
    headers.addReferenceKey(
        Http::Headers::get().SetCookie,
        absl::StrCat(fmt::format(CookieDeleteFormatString, config_->cookieNames().bearer_token_,
                                 config_->bearerTokenCookieSettings().path_),
                     cookie_domain));
  }

  if (!id_token_.empty()) {
    headers.addReferenceKey(
        Http::Headers::get().SetCookie,
        absl::StrCat(cookie_names.id_token_, "=", encryptToken(id_token_),
                     buildCookieTail(config_->idTokenCookieSettings(), expires_id_token_in_)));
  } else if (request_cookies.contains(cookie_names.id_token_)) {
    headers.addReferenceKey(
        Http::Headers::get().SetCookie,
        absl::StrCat(fmt::format(CookieDeleteFormatString, config_->cookieNames().id_token_,
                                 config_->idTokenCookieSettings().path_),
                     cookie_domain));
  }

  if (!refresh_token_.empty()) {
    headers.addReferenceKey(Http::Headers::get().SetCookie,
                            absl::StrCat(cookie_names.refresh_token_, "=",
                                         encryptToken(refresh_token_),
                                         buildCookieTail(config_->refreshTokenCookieSettings(),
                                                         expires_refresh_token_in_)));
  } else if (request_cookies.contains(cookie_names.refresh_token_)) {
    headers.addReferenceKey(
        Http::Headers::get().SetCookie,
        absl::StrCat(fmt::format(CookieDeleteFormatString, config_->cookieNames().refresh_token_,
                                 config_->refreshTokenCookieSettings().path_),
                     cookie_domain));
  }
}

void OAuth2Filter::addFlowCookieDeletionHeaders(Http::ResponseHeaderMap& headers,
                                                absl::string_view flow_id) const {
  const CookieNames& cookie_names = config_->cookieNames();
  std::string cookie_domain;
  if (!config_->cookieDomain().empty()) {
    cookie_domain = fmt::format(CookieDomainFormatString, config_->cookieDomain());
  }

  auto add_delete_cookie = [&](absl::string_view cookie_name, absl::string_view cookie_path) {
    const bool add_secure_attr =
        cookie_name.starts_with("__Secure-") || cookie_name.starts_with("__Host-");
    const absl::string_view maybe_secure_attr = add_secure_attr ? "; Secure" : "";

    headers.addReferenceKey(
        Http::Headers::get().SetCookie,
        absl::StrCat(fmt::format(CookieDeleteFormatString, cookie_name, cookie_path), cookie_domain,
                     maybe_secure_attr));
  };

  auto add_delete_cookie_variants = [&](absl::string_view base_name,
                                        absl::string_view cookie_path) {
    if (!flow_id.empty()) {
      add_delete_cookie(cookieNameWithSuffix(base_name, flow_id), cookie_path);
    }
    // Always delete the legacy unsuffixed cookie for mixed-version clusters.
    // TODO(Huabing): Remove once all supported releases understand suffixed names.
    add_delete_cookie(base_name, cookie_path);
  };

  add_delete_cookie_variants(cookie_names.oauth_nonce_, config_->nonceCookieSettings().path_);
  add_delete_cookie_variants(cookie_names.code_verifier_,
                             config_->codeVerifierCookieSettings().path_);
}

void OAuth2Filter::sendUnauthorizedResponse(const std::string& details) {
  ENVOY_LOG(warn, "Responding with 401 Unauthorized. Cause: {}", details);
  config_->stats().oauth_failure_.inc();
  decoder_callbacks_->sendLocalReply(
      Http::Code::Unauthorized, UnauthorizedBodyMessage,
      [this](Http::ResponseHeaderMap& headers) {
        if (!flow_id_.empty()) {
          // Delete the csrf and code_verifier cookies if set.
          // These cookies are no longer needed and should be deleted to prevent bloating the
          // requests.
          addFlowCookieDeletionHeaders(headers, flow_id_);
        }
      },
      absl::nullopt, details);
}

// Validates the OAuth callback request.
// * Does the query parameters contain an error response?
// * Does the query parameters contain the code and state?
// * Does the state parameter contain the original request URL, CSRF token, and flow id?
CallbackValidationResult
OAuth2Filter::validateOAuthCallback(const Http::RequestHeaderMap& headers,
                                    const absl::string_view path_str) const {
  // Return 401 unauthorized if the query parameters contain an error response.
  const auto query_parameters = Http::Utility::QueryParamsMulti::parseQueryString(path_str);
  if (query_parameters.getFirstValue(queryParamsError).has_value()) {
    // Attempt to extract the flow_id from the state parameter so that we can delete the
    // corresponding flow cookies when sending the unauthorized response.
    //
    // According to OAuth 2.0 spec, the state parameter must be present in an error response if it
    // was sent in the authorization request.
    // Reference: https://datatracker.ietf.org/doc/html/rfc6749#section-4.1.2.1
    std::string flow_id;
    auto stateVal = query_parameters.getFirstValue(queryParamsState);
    if (stateVal.has_value()) {
      CallbackValidationResult result = validateState(headers, stateVal.value());
      flow_id = result.flow_id_;
    }
    return {false, "", "", flow_id,
            fmt::format("OAuth server returned an error: {}", query_parameters.toString())};
  }

  // Return 401 unauthorized if the query parameters do not contain the code and state.
  auto codeVal = query_parameters.getFirstValue(queryParamsCode);
  auto stateVal = query_parameters.getFirstValue(queryParamsState);
  if (!codeVal.has_value() || !stateVal.has_value()) {
    return {
        false, "", "", "",
        fmt::format("Code or state query param does not exist: {}", query_parameters.toString())};
  }

  // Return 401 unauthorized if the state query parameter does not contain the original request
  // URL or the CSRF token. Decode the state parameter to get the original request URL and the
  // CSRF token.
  CallbackValidationResult result = validateState(headers, stateVal.value());
  result.auth_code_ = codeVal.value();
  return result;
}

// Validates the state parameter in the OAuth callback request.
CallbackValidationResult OAuth2Filter::validateState(const Http::RequestHeaderMap& headers,
                                                     const absl::string_view state_str) const {
  // Return 401 unauthorized if the state query parameter does not contain the original request
  // URL or the CSRF token. Decode the state parameter to get the original request URL and the
  // CSRF token.
  const std::string state = Base64Url::decode(state_str);
  bool has_unknown_field;
  Protobuf::Struct message;

  auto status = MessageUtil::loadFromJsonNoThrow(state, message, has_unknown_field);
  if (!status.ok()) {
    return {false, "", "", "", fmt::format("State query param is not a valid JSON: {}", state)};
  }

  // TODO(huabing): make the flow_id mandatory in the state parameter once all supported releases
  // understand suffixed cookie names.
  const auto& filed_value_pair = message.fields();
  if (!filed_value_pair.contains(stateParamsUrl) ||
      !filed_value_pair.contains(stateParamsCsrfToken)) {
    return {false, "", "", "",
            fmt::format("State query param does not contain url or CSRF token: {}", state)};
  }

  // Return 401 unauthorized if the CSRF token cookie does not match the CSRF token in the state.
  //
  // This is to prevent attackers from injecting their own access token into a victim's
  // sessions via CSRF attack. The attack can result in victims saving their sensitive data
  // in the attacker's account.
  // More information can be found at https://datatracker.ietf.org/doc/html/rfc6819#section-5.3.5
  std::string csrf_token = filed_value_pair.at(stateParamsCsrfToken).string_value();
  std::string flow_id = filed_value_pair.contains(stateParamsFlowId)
                            ? filed_value_pair.at(stateParamsFlowId).string_value()
                            : "";

  // We can't trust the flow_id from the state parameter without validating the CSRF token first.
  if (!validateCsrfToken(headers, csrf_token, flow_id)) {
    return {false, "", "", "", "CSRF token validation failed"};
  }

  // Return 401 unauthorized if the URL in the state is not valid.
  const std::string original_request_url = filed_value_pair.at(stateParamsUrl).string_value();
  Http::Utility::Url url;
  if (!url.initialize(original_request_url, false)) {
    return {false, "", "", flow_id,
            fmt::format("State url can not be initialized: {}", original_request_url)};
  }

  return {true, "", original_request_url, flow_id, ""};
}

// Validates the csrf_token in the state parameter against the one in the cookie.
bool OAuth2Filter::validateCsrfToken(const Http::RequestHeaderMap& headers,
                                     const std::string& csrf_token,
                                     absl::string_view flow_id) const {
  absl::optional<std::string> cookie_value =
      readCookieValueWithSuffix(headers, config_->cookieNames().oauth_nonce_, flow_id);
  if (!cookie_value.has_value()) {
    return false;
  }

  if (cookie_value.value() != csrf_token) {
    return false;
  }
  return validateCsrfTokenHmac(config_->hmacSecret(), csrf_token);
}

// Removes OAuth flow cookies from the request headers.
// These cookies are supposed to be used only in the OAuth2 flows and should not be exposed to the
// backend service.
// Keep the id_token and access_token cookies as they are user credentials and the backend service
// may expect them to be present.
// TODO: we many need a configuration knob in the OAuth2 filter to remove the id_token and
// access_token.
void OAuth2Filter::removeOAuthFlowCookies(Http::RequestHeaderMap& headers) const {
  absl::flat_hash_map<std::string, std::string> cookies = Http::Utility::parseCookies(headers);
  if (cookies.empty()) {
    return;
  }
  const CookieNames& cookie_names = config_->cookieNames();

  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.oauth2_cleanup_cookies")) {
    cookies.erase(cookie_names.oauth_hmac_);
    cookies.erase(cookie_names.oauth_expires_);
    cookies.erase(cookie_names.refresh_token_);

    auto eraseCookieWithSuffix = [&cookies](const std::string& base_name) {
      // Keep removing the legacy cookie name while we support mixed-version clusters.
      // TODO(Huabing): Delete only suffixed names once all supported releases understand suffixed
      // names.
      cookies.erase(base_name);
      const std::string prefix = absl::StrCat(base_name, CookieSuffixDelimiter);
      for (auto it = cookies.begin(); it != cookies.end();) {
        if (it->first.starts_with(prefix)) {
          cookies.erase(it++);
        } else {
          ++it;
        }
      }
    };

    eraseCookieWithSuffix(cookie_names.oauth_nonce_);
    eraseCookieWithSuffix(cookie_names.code_verifier_);

    std::string new_cookies(absl::StrJoin(cookies, "; ", absl::PairFormatter("=")));
    headers.setReferenceKey(Http::Headers::get().Cookie, new_cookies);
  }
}

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
