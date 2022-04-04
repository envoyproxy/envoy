#pragma once

#include <chrono>
#include <functional>
#include <memory>

#include "envoy/extensions/http/stateful_session/cookie/v3/cookie.pb.h"
#include "envoy/http/stateful_session.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"

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
                     const CookieBasedSessionStateFactory& factory)
        : upstream_address_(std::move(address)), factory_(factory) {}

    absl::optional<absl::string_view> upstreamAddress() const override { return upstream_address_; }
    void onUpdate(const Upstream::HostDescription& host,
                  Envoy::Http::ResponseHeaderMap& headers) override;

  private:
    absl::optional<std::string> upstream_address_;
    const CookieBasedSessionStateFactory& factory_;
  };

  CookieBasedSessionStateFactory(const CookieBasedSessionStateProto& config);

  Envoy::Http::SessionStatePtr create(const Envoy::Http::RequestHeaderMap& headers) const override {
    if (!requestPathMatch(headers.getPathValue())) {
      return nullptr;
    }

    return std::make_unique<SessionStateImpl>(parseAddress(headers), *this);
  }

  bool requestPathMatch(absl::string_view request_path) const {
    ASSERT(path_matcher_ != nullptr);
    return path_matcher_(request_path);
  }

private:
  absl::optional<std::string> parseAddress(const Envoy::Http::RequestHeaderMap& headers) const {
    const std::string cookie_value = Envoy::Http::Utility::parseCookieValue(headers, name_);
    std::string address = Envoy::Base64::decode(cookie_value);

    return !address.empty() ? absl::make_optional(std::move(address)) : absl::nullopt;
  }

  std::string makeSetCookie(const std::string& address) const {
    return Envoy::Http::Utility::makeSetCookieValue(name_, address, path_, ttl_, true);
  }

  const std::string name_;
  const std::chrono::seconds ttl_;
  const std::string path_;

  std::function<bool(absl::string_view)> path_matcher_;
};

} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
