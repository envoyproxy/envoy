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

class HeaderBasedSessionStateFactory : public Envoy::Http::SessionStateFactory,
                                       public Logger::Loggable<Logger::Id::http> {
public:
  class SessionStateImpl : public Envoy::Http::SessionState {
  public:
    SessionStateImpl(absl::optional<std::string> address,
                     const HeaderBasedSessionStateFactory& factory)
        : upstream_address_(std::move(address)), factory_(factory) {}

    absl::optional<absl::string_view> upstreamAddress() const override { return upstream_address_; }
    void onUpdate(absl::string_view host_address, Envoy::Http::ResponseHeaderMap& headers) override;

  private:
    absl::optional<std::string> upstream_address_;
    const HeaderBasedSessionStateFactory& factory_;
  };

  HeaderBasedSessionStateFactory(const HeaderBasedSessionStateProto& config);

  Envoy::Http::SessionStatePtr create(Envoy::Http::RequestHeaderMap& headers) const override {
    return std::make_unique<SessionStateImpl>(parseAddress(headers), *this);
  }

private:
  absl::optional<std::string> parseAddress(Envoy::Http::RequestHeaderMap& headers) const;

  const Envoy::Http::LowerCaseString name_;
  const HeaderBasedSessionStateProto::Mode mode_{};
};

} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
