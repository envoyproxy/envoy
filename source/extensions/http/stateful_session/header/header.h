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
    SessionStateImpl(std::optional<std::string> address,
                     const HeaderBasedSessionStateFactory& factory)
        : upstream_address_(std::move(address)), factory_(factory) {}

    std::optional<absl::string_view> upstreamAddress() const override { return upstream_address_; }
    bool onUpdate(absl::string_view host_address, Envoy::Http::ResponseHeaderMap& headers) override;

  private:
    std::optional<std::string> upstream_address_;
    const HeaderBasedSessionStateFactory& factory_;
  };

  HeaderBasedSessionStateFactory(const HeaderBasedSessionStateProto& config);

  Envoy::Http::SessionStatePtr create(Envoy::Http::RequestHeaderMap& headers) const override {
    return std::make_unique<SessionStateImpl>(parseAddress(headers), *this);
  }

private:
  std::optional<std::string> parseAddress(const Envoy::Http::RequestHeaderMap& headers) const {
    auto hdr = headers.get(Envoy::Http::LowerCaseString(name_));
    if (hdr.empty()) {
      return std::nullopt;
    }

    auto header_value = hdr[0]->value().getStringView();
    std::string address = Envoy::Base64::decode(header_value);
    return !address.empty() ? std::make_optional(std::move(address)) : std::nullopt;
  }

  const Envoy::Http::LowerCaseString& getHeaderName() const { return name_; }

  const Envoy::Http::LowerCaseString name_;
};

} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
