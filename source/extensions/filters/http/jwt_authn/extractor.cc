#include "extensions/filters/http/jwt_authn/extractor.h"

#include <memory>

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "common/common/utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/singleton/const_singleton.h"

#include "absl/strings/match.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtProvider;
using Envoy::Http::LowerCaseString;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

/**
 * Constant values
 */
struct JwtConstValueStruct {
  // The header value prefix for Authorization.
  const std::string BearerPrefix{"Bearer "};

  // The default query parameter name to extract JWT token
  const std::string AccessTokenParam{"access_token"};
};
using JwtConstValues = ConstSingleton<JwtConstValueStruct>;

// A base JwtLocation object to store token and specified_issuers.
class JwtLocationBase : public JwtLocation {
public:
  JwtLocationBase(const std::string& token, const std::unordered_set<std::string>& issuers)
      : token_(token), specified_issuers_(issuers) {}

  // Get the token string
  const std::string& token() const override { return token_; }

  // Check if an issuer has specified the location.
  bool isIssuerSpecified(const std::string& issuer) const override {
    return specified_issuers_.find(issuer) != specified_issuers_.end();
  }

private:
  // Extracted token.
  const std::string token_;
  // Stored issuers specified the location.
  const std::unordered_set<std::string>& specified_issuers_;
};

// The JwtLocation for header extraction.
class JwtHeaderLocation : public JwtLocationBase {
public:
  JwtHeaderLocation(const std::string& token, const std::unordered_set<std::string>& issuers,
                    const LowerCaseString& header)
      : JwtLocationBase(token, issuers), header_(header) {}

  void removeJwt(Http::HeaderMap& headers) const override { headers.remove(header_); }

private:
  // the header name the JWT is extracted from.
  const LowerCaseString& header_;
};

// The JwtLocation for param extraction.
class JwtParamLocation : public JwtLocationBase {
public:
  JwtParamLocation(const std::string& token, const std::unordered_set<std::string>& issuers,
                   const std::string&)
      : JwtLocationBase(token, issuers) {}

  void removeJwt(Http::HeaderMap&) const override {
    // TODO(qiwzhang): remove JWT from parameter.
  }
};

/**
 * The class implements Extractor interface
 *
 */
class ExtractorImpl : public Logger::Loggable<Logger::Id::jwt>, public Extractor {
public:
  ExtractorImpl(const JwtProvider& provider);

  ExtractorImpl(
      const std::vector<const envoy::extensions::filters::http::jwt_authn::v3::JwtProvider*>&
          providers);

  std::vector<JwtLocationConstPtr> extract(const Http::RequestHeaderMap& headers) const override;

  void sanitizePayloadHeaders(Http::HeaderMap& headers) const override;

private:
  // add a header config
  void addHeaderConfig(const std::string& issuer, const Http::LowerCaseString& header_name,
                       const std::string& value_prefix);
  // add a query param config
  void addQueryParamConfig(const std::string& issuer, const std::string& param);
  // ctor helper for a jwt provider config
  void addProvider(const JwtProvider& provider);

  // @return what should be the 3-part base64url-encoded substring; see RFC-7519
  absl::string_view extractJWT(absl::string_view value_str,
                               absl::string_view::size_type after) const;

  // HeaderMap value type to store prefix and issuers that specified this
  // header.
  struct HeaderLocationSpec {
    HeaderLocationSpec(const Http::LowerCaseString& header, const std::string& value_prefix)
        : header_(header), value_prefix_(value_prefix) {}
    // The header name.
    Http::LowerCaseString header_;
    // The value prefix. e.g. for "Bearer <token>", the value_prefix is "Bearer ".
    std::string value_prefix_;
    // Issuers that specified this header.
    std::unordered_set<std::string> specified_issuers_;
  };
  using HeaderLocationSpecPtr = std::unique_ptr<HeaderLocationSpec>;
  // The map of (header + value_prefix) to HeaderLocationSpecPtr
  std::map<std::string, HeaderLocationSpecPtr> header_locations_;

  // ParamMap value type to store issuers that specified this header.
  struct ParamLocationSpec {
    // Issuers that specified this param.
    std::unordered_set<std::string> specified_issuers_;
  };
  // The map of a parameter key to set of issuers specified the parameter
  std::map<std::string, ParamLocationSpec> param_locations_;

  std::vector<LowerCaseString> forward_payload_headers_;
};

ExtractorImpl::ExtractorImpl(const JwtProvider& provider) { addProvider(provider); }

ExtractorImpl::ExtractorImpl(const JwtProviderList& providers) {
  for (const auto& provider : providers) {
    ASSERT(provider);
    addProvider(*provider);
  }
}

void ExtractorImpl::addProvider(const JwtProvider& provider) {
  for (const auto& header : provider.from_headers()) {
    addHeaderConfig(provider.issuer(), LowerCaseString(header.name()), header.value_prefix());
  }
  for (const std::string& param : provider.from_params()) {
    addQueryParamConfig(provider.issuer(), param);
  }
  // If not specified, use default locations.
  if (provider.from_headers().empty() && provider.from_params().empty()) {
    addHeaderConfig(provider.issuer(), Http::CustomHeaders::get().Authorization,
                    JwtConstValues::get().BearerPrefix);
    addQueryParamConfig(provider.issuer(), JwtConstValues::get().AccessTokenParam);
  }
  if (!provider.forward_payload_header().empty()) {
    forward_payload_headers_.emplace_back(provider.forward_payload_header());
  }
}

void ExtractorImpl::addHeaderConfig(const std::string& issuer, const LowerCaseString& header_name,
                                    const std::string& value_prefix) {
  ENVOY_LOG(debug, "addHeaderConfig for issuer {} at {}", issuer, header_name.get());
  const std::string map_key = header_name.get() + value_prefix;
  auto& header_location_spec = header_locations_[map_key];
  if (!header_location_spec) {
    header_location_spec = std::make_unique<HeaderLocationSpec>(header_name, value_prefix);
  }
  header_location_spec->specified_issuers_.insert(issuer);
}

void ExtractorImpl::addQueryParamConfig(const std::string& issuer, const std::string& param) {
  auto& param_location_spec = param_locations_[param];
  param_location_spec.specified_issuers_.insert(issuer);
}

std::vector<JwtLocationConstPtr>
ExtractorImpl::extract(const Http::RequestHeaderMap& headers) const {
  std::vector<JwtLocationConstPtr> tokens;

  // Check header locations first
  for (const auto& location_it : header_locations_) {
    const auto& location_spec = location_it.second;
    ENVOY_LOG(debug, "extract {}", location_it.first);
    const Http::HeaderEntry* entry = headers.get(location_spec->header_);
    if (entry) {
      auto value_str = entry->value().getStringView();
      if (!location_spec->value_prefix_.empty()) {
        const auto pos = value_str.find(location_spec->value_prefix_);
        if (pos == absl::string_view::npos) {
          // value_prefix not found anywhere in value_str, so skip
          continue;
        }
        value_str = extractJWT(value_str, pos + location_spec->value_prefix_.length());
      }
      tokens.push_back(std::make_unique<const JwtHeaderLocation>(
          std::string(value_str), location_spec->specified_issuers_, location_spec->header_));
    }
  }

  // If no query parameter locations specified, or Path() is null, bail out
  if (param_locations_.empty() || headers.Path() == nullptr) {
    return tokens;
  }

  // Check query parameter locations.
  const auto& params = Http::Utility::parseQueryString(headers.getPathValue());
  for (const auto& location_it : param_locations_) {
    const auto& param_key = location_it.first;
    const auto& location_spec = location_it.second;
    const auto& it = params.find(param_key);
    if (it != params.end()) {
      tokens.push_back(std::make_unique<const JwtParamLocation>(
          it->second, location_spec.specified_issuers_, param_key));
    }
  }
  return tokens;
}

// as specified in RFC-4648 § 5, plus dot (period, 0x2e), of which two are required in the JWT
constexpr absl::string_view ConstantBase64UrlEncodingCharsPlusDot =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";

// Returns a token, not a URL: skips non-Base64Url-legal (or dot) characters, collects following
// Base64Url+dot string until first non-Base64Url char.
//
// The input parameters:
//    "value_str" - the header value string, perhaps "Bearer string....", and
//    "after" - the offset into that string after which to begin looking for JWT-legal characters
//
// For backwards compatibility, if it finds no suitable string, it returns value_str as-is.
//
// It is forgiving w.r.t. dots/periods, as the exact syntax will be verified after extraction.
//
// See RFC-7519 § 2, RFC-7515 § 2, and RFC-4648 "Base-N Encodings" § 5.
absl::string_view ExtractorImpl::extractJWT(absl::string_view value_str,
                                            absl::string_view::size_type after) const {
  const auto starting = value_str.find_first_of(ConstantBase64UrlEncodingCharsPlusDot, after);
  if (starting == value_str.npos) {
    return value_str;
  }
  // There should be two dots (periods; 0x2e) inside the string, but we don't verify that here
  auto ending = value_str.find_first_not_of(ConstantBase64UrlEncodingCharsPlusDot, starting);
  if (ending == value_str.npos) { // Base64Url-encoded string occupies the rest of the line
    return value_str.substr(starting);
  }
  return value_str.substr(starting, ending - starting);
}

void ExtractorImpl::sanitizePayloadHeaders(Http::HeaderMap& headers) const {
  for (const auto& header : forward_payload_headers_) {
    headers.remove(header);
  }
}

} // namespace

ExtractorConstPtr Extractor::create(const JwtProvider& provider) {
  return std::make_unique<ExtractorImpl>(provider);
}

ExtractorConstPtr Extractor::create(const JwtProviderList& providers) {
  return std::make_unique<ExtractorImpl>(providers);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
