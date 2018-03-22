#include "common/jwt_authn/extractor.h"

#include "common/common/utility.h"
#include "common/http/utility.h"


using envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;

namespace Envoy {
namespace JwtAuthn {
namespace {

// The autorization bearer prefix.
const std::string BearerPrefix = "Bearer ";

// The query parameter name to get JWT token.
const std::string ParamAccessToken = "access_token";

} // namespace

extractor::extractor(const JwtAuthentication& config) {
  for (const auto& rule : config.rules()) {
    bool use_default = true;
    if (rule.from_headers_size() > 0) {
      use_default = false;
      for (const auto& header : rule.from_headers()) {
        auto& issuers = header_maps_[LowerCaseString(header.name())];
        issuers.insert(rule.issuer());
      }
    }
    if (jwt.jwt_params_size() > 0) {
      use_default = false;
      for (const std::string& param : jwt.jwt_params()) {
        auto& issuers = param_maps_[param];
        issuers.insert(jwt.issuer());
      }
    }

    // If not specified, use default
    if (use_default) {
      authorization_issuers_.insert(jwt.issuer());

      auto& param_issuers = param_maps_[kParamAccessToken];
      param_issuers.insert(jwt.issuer());
    }
  }
}

void extractor::extract(
    const HeaderMap& headers,
    std::vector<std::unique_ptr<extractor::Token>>* tokens) const {
  if (!authorization_issuers_.empty()) {
    const HeaderEntry* entry = headers.Authorization();
    if (entry) {
      // Extract token from header.
      const HeaderString& value = entry->value();
      if (StringUtil::startsWith(value.c_str(), kBearerPrefix, true)) {
        tokens->emplace_back(new Token(value.c_str() + kBearerPrefix.length(),
                                       authorization_issuers_, true, nullptr));
        // Only take the first one.
        return;
      }
    }
  }

  // Check header first
  for (const auto& header_it : header_maps_) {
    const HeaderEntry* entry = headers.get(header_it.first);
    if (entry) {
      tokens->emplace_back(new Token(std::string(entry->value().c_str(), entry->value().size()),
                                     header_it.second, false, &header_it.first));
      // Only take the first one.
      return;
    }
  }

  if (param_maps_.empty() || headers.Path() == nullptr) {
    return;
  }

  const auto& params = Utility::parseQueryString(
      std::string(headers.Path()->value().c_str(), headers.Path()->value().size()));
  for (const auto& param_it : param_maps_) {
    const auto& it = params.find(param_it.first);
    if (it != params.end()) {
      tokens->emplace_back(new Token(it->second, param_it.second, false, nullptr));
      // Only take the first one.
      return;
    }
  }
}

} // namespace JwtAuthn
} // namespace Envoy
