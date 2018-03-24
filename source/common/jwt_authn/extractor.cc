#include "common/jwt_authn/extractor.h"

#include "common/common/utility.h"
#include "common/http/utility.h"

using ::Envoy::Http::LowerCaseString;
using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;

namespace Envoy {
namespace JwtAuthn {
namespace {

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
  std::string token_;
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
                   const std::string& param)
      : JwtLocationBase(token, issuers), param_(param) {}

  void removeJwt(Http::HeaderMap&) const override {
    // TODO(qiwzhang): remove JWT from parameter.
  }

private:
  // the parameter key the JWT is extracted from.
  const std::string& param_;
};

} // namespace

Extractor::Extractor(const JwtAuthentication& config) {
  for (const auto& rule : config.rules()) {
    for (const auto& header : rule.from_headers()) {
      addHeaderConfig(rule.issuer(), LowerCaseString(header.name()), header.value_prefix());
    }
    for (const std::string& param : rule.from_params()) {
      addParamConfig(rule.issuer(), param);
    }

    // If not specified, use default locations.
    if (rule.from_headers().empty() && rule.from_params().empty()) {
      addHeaderConfig(rule.issuer(), Http::Headers::get().Authorization,
                      Http::Headers::get().JwtValues.BearerPrefix);
      addParamConfig(rule.issuer(), Http::Headers::get().JwtValues.AccessTokenParam);
    }
  }
}

void Extractor::addHeaderConfig(const std::string& issuer, const LowerCaseString& header_name,
                                const std::string& value_prefix) {
  std::string key = header_name.get() + value_prefix;
  auto it = header_maps_.empace
  map_value.header_ = header_name;
  map_value.value_prefix_ = value_prefix;
  map_value.specified_issuers_.insert(issuer);
}

void Extractor::addParamConfig(const std::string& issuer, const std::string& param) {
  auto& map_value = param_maps_[param];
  map_value.specified_issuers_.insert(issuer);
}

std::vector<JwtLocationPtr> Extractor::extract(const Http::HeaderMap& headers) const {
  std::vector<JwtLocationPtr> tokens;
  // Check header first
  for (const auto& header_it : header_maps_) {
    const auto& map_value = header_it.second;
    const Http::HeaderEntry* entry = headers.get(map_value.header_);
    if (entry) {
      std::string value_str = std::string(entry->value().c_str(), entry->value().size());
      if (!map_value.value_prefix_.empty()) {
        if (!StringUtil::startsWith(value_str.c_str(), map_value.value_prefix_, true)) {
          // prefix doesn't match, skip it.
          continue;
        }
        value_str = value_str.substr(map_value.value_prefix_.size());
      }
      tokens.emplace_back(
          new JwtHeaderLocation(value_str, map_value.specified_issuers_, map_value.header_));
    }
  }

  if (param_maps_.empty() || headers.Path() == nullptr) {
    return tokens;
  }

  const auto& current_params = Http::Utility::parseQueryString(
      std::string(headers.Path()->value().c_str(), headers.Path()->value().size()));
  for (const auto& param_it : param_maps_) {
    const auto& map_key = param_it.first;
    const auto& map_value = param_it.second;
    const auto& it = current_params.find(map_key);
    if (it != current_params.end()) {
      tokens.emplace_back(new JwtParamLocation(it->second, map_value.specified_issuers_, map_key));
    }
  }
  return tokens;
}

} // namespace JwtAuthn
} // namespace Envoy
