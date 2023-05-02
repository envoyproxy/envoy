#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "envoy/extensions/http/stateful_session/cookie/v3/cookie.pb.h"
#include "envoy/http/stateful_session.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/extensions/http/stateful_session/cookie/cookie.pb.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Cookie {

using CookieBasedSessionStateProto =
    envoy::extensions::http::stateful_session::cookie::v3::CookieBasedSessionState;

class CookieBasedSessionStateFactory : public Envoy::Http::SessionStateFactory {
public:
  class SessionStateImpl : public Envoy::Http::SessionState {
  public:
    SessionStateImpl(absl::optional<std::string> address,
                     const CookieBasedSessionStateFactory& factory, TimeSource& time_source)
        : upstream_address_(std::move(address)), factory_(factory), time_source_(time_source) {}

    absl::optional<absl::string_view> upstreamAddress() const override { return upstream_address_; }
    void onUpdate(const Upstream::HostDescription& host,
                  Envoy::Http::ResponseHeaderMap& headers) override;
    void setEncodeStyle(bool style) { use_old_style_encoding_ = style; }

  private:
    absl::optional<std::string> upstream_address_;
    const CookieBasedSessionStateFactory& factory_;
    TimeSource& time_source_;
    bool use_old_style_encoding_{false};
  };

  CookieBasedSessionStateFactory(const CookieBasedSessionStateProto& config,
                                 TimeSource& time_source);

  Envoy::Http::SessionStatePtr create(const Envoy::Http::RequestHeaderMap& headers) const override {
    if (!requestPathMatch(headers.getPathValue())) {
      return nullptr;
    }

    const auto address = parseAddress(headers);
    auto sessionState = std::make_unique<SessionStateImpl>(address.first, *this, time_source_);
    if (address.first != absl::nullopt) {
      sessionState->setEncodeStyle(address.second);
    }
    return sessionState;
  }

  bool requestPathMatch(absl::string_view request_path) const {
    ASSERT(path_matcher_ != nullptr);
    return path_matcher_(request_path);
  }

private:
  std::pair<absl::optional<std::string>, bool>
  parseAddress(const Envoy::Http::RequestHeaderMap& headers) const {
    bool use_old_style_encoding = false;
    const std::string cookie_value = Envoy::Http::Utility::parseCookieValue(headers, name_);
    if (cookie_value.empty()) {
      return std::make_pair(absl::nullopt, use_old_style_encoding);
    }
    const std::string decoded_value = Envoy::Base64::decode(cookie_value);
    std::string address;

    // Try to interpret the cookie as proto payload.
    // Otherwise treat it as "old" style format, which is ip-address:port.
    envoy::Cookie cookie;
    if (cookie.ParseFromString(decoded_value)) {
      address = cookie.address();
      if (address.empty() || (cookie.expires() == 0)) {
        return std::make_pair(absl::nullopt, use_old_style_encoding);
      }

      std::chrono::seconds expiry_time(cookie.expires());
      auto now = std::chrono::duration_cast<std::chrono::seconds>(
          (time_source_.monotonicTime()).time_since_epoch());
      if (now > expiry_time) {
        // Ignore the address extracted from the cookie. This will cause
        // upstream cluster to select a new host and new cookie will be generated.
        return std::make_pair(absl::nullopt, use_old_style_encoding);
      }
    } else {
      ENVOY_LOG_ONCE_MISC(
          warn, "Non-proto cookie format detected. This format will be rejected in the future.");
      // Treat this as "old" style cookie.
      address = decoded_value;
      use_old_style_encoding = true;
    }

    return std::make_pair(!address.empty() ? absl::make_optional(std::move(address))
                                           : absl::nullopt,
                          use_old_style_encoding);
  }

  std::string makeSetCookie(const std::string& address) const {
    return Envoy::Http::Utility::makeSetCookieValue(name_, address, path_, ttl_, true);
  }

  const std::string name_;
  const std::chrono::seconds ttl_;
  const std::string path_;
  TimeSource& time_source_;

  std::function<bool(absl::string_view)> path_matcher_;
};

} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
