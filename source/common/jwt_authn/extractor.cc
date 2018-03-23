#include "common/jwt_authn/extractor.h"

#include "common/common/utility.h"
#include "common/http/utility.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;

namespace Envoy {
namespace JwtAuthn {
namespace {

// The authorization header name.
const std::string AuthorizationHeader = "authorization";

// The authorization bearer prefix.
const std::string BearerPrefix = "Bearer ";

// The query parameter name to get JWT token.
const std::string ParamAccessToken = "access_token";

} // namespace

Extractor::Extractor(const JwtAuthentication& config) {
  for (const auto& rule : config.rules()) {
    bool use_default = true;
    if (rule.from_headers_size() > 0) {
      use_default = false;
      for (const auto& header : rule.from_headers()) {
        config_header(rule.issuer(), header.name(), header.value_prefix());
      }
    }
    if (rule.from_params_size() > 0) {
      use_default = false;
      for (const std::string& param : rule.from_params()) {
        config_param(rule.issuer(), param);
      }
    }

    // If not specified, use default locations.
    if (use_default) {
      config_header(rule.issuer(), AuthorizationHeader, BearerPrefix);
      config_param(rule.issuer(), ParamAccessToken);
    }
  }
}

void Extractor::config_header(const std::string& issuer, const std::string& header_name,
                              const std::string& value_prefix) {
  auto& map_value = header_maps_[Http::LowerCaseString(header_name)];
  map_value.value_prefix_ = value_prefix;
  map_value.specified_issuers_.insert(issuer);
}

void Extractor::config_param(const std::string& issuer, const std::string& param) {
  auto& map_value = param_maps_[param];
  map_value.specified_issuers_.insert(issuer);
}

void Extractor::extract(const Http::HeaderMap& headers, std::vector<JwtLocationPtr>* tokens) const {
  // Check header first
  for (const auto& header_it : header_maps_) {
    const auto& map_key = header_it.first;
    const auto& map_value = header_it.second;
    const Http::HeaderEntry* entry = headers.get(map_key);
    if (entry) {
      std::string value_str = std::string(entry->value().c_str(), entry->value().size());
      if (!map_value.value_prefix_.empty()) {
        if (!StringUtil::startsWith(value_str.c_str(), map_value.value_prefix_, true)) {
          // prefix doesn't match, skip it.
          continue;
        }
        value_str = value_str.substr(map_value.value_prefix_.size());
      }
      tokens->emplace_back(new JwtLocation(value_str, map_value.specified_issuers_, &map_key));
    }
  }

  if (param_maps_.empty() || headers.Path() == nullptr) {
    return;
  }

  const auto& current_params = Http::Utility::parseQueryString(
      std::string(headers.Path()->value().c_str(), headers.Path()->value().size()));
  for (const auto& param_it : param_maps_) {
    const auto& map_key = param_it.first;
    const auto& map_value = param_it.second;
    const auto& it = current_params.find(map_key);
    if (it != current_params.end()) {
      tokens->emplace_back(new JwtLocation(it->second, map_value.specified_issuers_, nullptr));
    }
  }
}

} // namespace JwtAuthn
} // namespace Envoy
