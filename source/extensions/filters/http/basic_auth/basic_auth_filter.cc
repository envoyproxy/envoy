#include "source/extensions/filters/http/basic_auth/basic_auth_filter.h"

#include <openssl/sha.h>

#include "source/common/common/base64.h"
#include "source/common/http/headers.h"
#include "source/common/http/header_utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BasicAuth {

namespace {

// Function to compute SHA1 hash
std::string computeSHA1(const std::string& password) {
  unsigned char hash[SHA_DIGEST_LENGTH];

  // Calculate the SHA-1 hash
  SHA1(reinterpret_cast<const unsigned char*>(password.c_str()), password.length(), hash);

  // Encode the binary hash in Base64
  std::string encodedHash = Base64::encode(reinterpret_cast<const char*>(hash), SHA_DIGEST_LENGTH);

  return encodedHash;
}

} // namespace

FilterConfig::FilterConfig(std::vector<User> users, const std::string& stats_prefix,
                           Stats::Scope& scope)
    : users_(users), stats_(generateStats(stats_prefix + "basic_auth.", scope)) {}

bool FilterConfig::validateUser(const std::string& username, const std::string& password) {
  for (auto user : users_) {
    if (user.name == username) {
      std::string hashedPassword = computeSHA1(password);
      if (hashedPassword == user.hash) {
        return true;
      }
      return false;
    }
  }
  return false;
}

BasicAuthFilter::BasicAuthFilter(FilterConfigSharedPtr config) : config_(std::move(config)) {}

Http::FilterHeadersStatus BasicAuthFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);

  auto auth_header = headers.get(Http::CustomHeaders::get().Authorization);
  if (!auth_header.empty()) {
    auto auth_value = auth_header[0]->value().getStringView();

    if (auth_value.substr(0, 6) == "Basic ") {
      // Extract and decode the Base64 part of the header.
      auto base64Token = auth_value.substr(6);
      std::string decoded = Base64::decodeWithoutPadding(base64Token);

      // The decoded string is in the format "username:password".
      size_t colonPos = decoded.find(':');

      if (colonPos != std::string::npos) {
        std::string username = decoded.substr(0, colonPos);
        std::string password = decoded.substr(colonPos + 1);

        if (config_->validateUser(username, password)) {
          config_->stats().allowed_.inc();
          return Http::FilterHeadersStatus::Continue;
        } else {
          config_->stats().denied_.inc();
          decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Basic Auth failed", nullptr,
                                             absl::nullopt, "");
          return Http::FilterHeadersStatus::StopIteration;
        }
      }
    }
  }

  config_->stats().denied_.inc();
  decoder_callbacks_->sendLocalReply(Http::Code::Unauthorized, "Missing username or password",
                                     nullptr, absl::nullopt, "");
  return Http::FilterHeadersStatus::StopIteration;
}

void BasicAuthFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  ENVOY_LOG(debug, "Called Filter : {}", __func__);
  decoder_callbacks_ = &callbacks;
}

} // namespace BasicAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
