#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "envoy/extensions/http/stateful_session/cookie/v3/cookie.pb.h"
#include "envoy/http/hash_policy.h"
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

  private:
    absl::optional<std::string> upstream_address_;
    const CookieBasedSessionStateFactory& factory_;
    TimeSource& time_source_;
  };

  CookieBasedSessionStateFactory(const CookieBasedSessionStateProto& config,
                                 TimeSource& time_source);

  Envoy::Http::SessionStatePtr create(const Envoy::Http::RequestHeaderMap& headers) const override {
    if (!requestPathMatch(headers.getPathValue())) {
      return nullptr;
    }

    return std::make_unique<SessionStateImpl>(parseAddress(headers), *this, time_source_);
  }

  bool requestPathMatch(absl::string_view request_path) const {
    ASSERT(path_matcher_ != nullptr);
    return path_matcher_(request_path);
  }

private:
  absl::optional<std::string> parseAddress(const Envoy::Http::RequestHeaderMap& headers) const {
    const std::string cookie_value = Envoy::Http::Utility::parseCookieValue(headers, name_);
    if (cookie_value.empty()) {
      return absl::nullopt;
    }
    const std::string decoded_value = Envoy::Base64::decode(cookie_value);
    std::string address;

    // Try to interpret the cookie as proto payload.
    // Otherwise treat it as "old" style format, which is ip-address:port.
    envoy::Cookie cookie;
    if (cookie.ParseFromString(decoded_value)) {
      address = cookie.address();
      if (address.empty()) {
        return absl::nullopt;
      }

      if (cookie.expires() != 0) {
        const std::chrono::seconds expiry_time(cookie.expires());
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            (time_source_.monotonicTime()).time_since_epoch());
        if (now > expiry_time) {
          // Ignore the address extracted from the cookie. This will cause
          // upstream cluster to select a new host and new cookie will be generated.
          return absl::nullopt;
        }
      }
    } else {
      ENVOY_LOG_ONCE_MISC(
          warn, "Non-proto cookie format detected. This format will be rejected in the future.");
      address = decoded_value;
    }

    return address.empty() ? absl::nullopt : absl::make_optional(std::move(address));
  }

  std::string makeSetCookie(const std::string& address) const {
    return Envoy::Http::Utility::makeSetCookieValue(name_, address, path_, ttl_, true, attributes_);
  }

  const std::string name_;
  const std::chrono::seconds ttl_;
  const std::string path_;
  const Envoy::Http::CookieAttributeRefVector attributes_;
  TimeSource& time_source_;

  std::function<bool(absl::string_view)> path_matcher_;
};

} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
