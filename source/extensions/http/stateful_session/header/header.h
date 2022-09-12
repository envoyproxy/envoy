#pragma once

#include <memory>
#include <tuple>

#include "envoy/extensions/http/stateful_session/header/v3/header.pb.h"
#include "envoy/http/stateful_session.h"

#include "source/common/common/base64.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Header {

using HeaderBasedSessionStateProto =
    envoy::extensions::http::stateful_session::header::v3::HeaderBasedSessionState;

class HeaderBasedSessionStateFactory : public Envoy::Http::SessionStateFactory {
public:
  class SessionStateImpl : public Envoy::Http::SessionState {
  public:
    SessionStateImpl(absl::optional<std::string> address) : upstream_address_(std::move(address)) {}

    absl::optional<absl::string_view> upstreamAddress() const override { return upstream_address_; }
    void onUpdate(const Upstream::HostDescription& host,
                  Envoy::Http::ResponseHeaderMap& headers) override;

  private:
    absl::optional<std::string> upstream_address_;
  };

  HeaderBasedSessionStateFactory(const HeaderBasedSessionStateProto& config);

  Envoy::Http::SessionStatePtr create(const Envoy::Http::RequestHeaderMap& headers) const override {
    if (!requestPathMatch(headers.getPathValue())) {
      return nullptr;
    }
    return std::make_unique<SessionStateImpl>(parseAddress(headers));
  }

  bool requestPathMatch(absl::string_view request_path) const {
    ASSERT(path_matcher_ != nullptr);
    return path_matcher_(request_path);
  }

private:
  absl::optional<std::string> parseAddress(const Envoy::Http::RequestHeaderMap& headers) const {
    std::string address;
    auto he = headers.STSHost();
    if (he != nullptr) {
      auto header_value = he->value().getStringView();
      address = Envoy::Base64::decode(header_value);
    }

    return !address.empty() ? absl::make_optional(std::move(address)) : absl::nullopt;
  }

  const std::string name_;
  const std::string path_;

  std::function<bool(absl::string_view)> path_matcher_;
};

} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
