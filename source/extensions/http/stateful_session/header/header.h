#pragma once

#include <memory>
#include <tuple>

#include "envoy/extensions/http/stateful_session/header/v3/header.pb.h"
#include "envoy/http/stateful_session.h"

#include "source/common/common/base64.h"
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
        SessionStateImpl(absl::optional<std::string> address,
                         const HeaderBasedSessionStateFactory& factory)
            : upstream_address_(std::move(address)), factory_(factory) {
                std::ignore = factory_;
            } 

        absl::optional<absl::string_view> upstreamAddress() const override {return upstream_address_; }
        void onUpdate(const Upstream::HostDescription& host,
                      Envoy::Http::ResponseHeaderMap& headers) override;
    
    private:
        absl::optional<std::string> upstream_address_;
        const HeaderBasedSessionStateFactory& factory_;
    };

    HeaderBasedSessionStateFactory(const HeaderBasedSessionStateProto& config);

    Envoy::Http::SessionStatePtr create(const Envoy::Http::RequestHeaderMap& headers) const override {
        return std::make_unique<SessionStateImpl>(parseAddress(headers), *this);
    }

private:
    absl::optional<std::string> parseAddress(const Envoy::Http::RequestHeaderMap& headers) const {
        std::ignore = headers;
        return "0.0.0.0:8080"; // TODO - implement
    }

    const std::string name_;
};

} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy