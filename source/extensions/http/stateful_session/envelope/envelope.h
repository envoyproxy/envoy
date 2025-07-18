#pragma once

#include <memory>
#include <tuple>

#include "envoy/extensions/http/stateful_session/envelope/v3/envelope.pb.h"
#include "envoy/http/stateful_session.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base64.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Envelope {

using EnvelopeSessionStateProto =
    envoy::extensions::http::stateful_session::envelope::v3::EnvelopeSessionState;

class EnvelopeSessionStateFactory : public Envoy::Http::SessionStateFactory,
                                    public Logger::Loggable<Logger::Id::http> {
  friend class SessionStateImpl;

public:
  class SessionStateImpl : public Envoy::Http::SessionState {
  public:
    SessionStateImpl(absl::optional<std::string> address,
                     const EnvelopeSessionStateFactory& factory)
        : upstream_address_(std::move(address)), factory_(factory) {}

    absl::optional<absl::string_view> upstreamAddress() const override { return upstream_address_; }
    void onUpdateHeader(absl::string_view host_address,
                        Envoy::Http::ResponseHeaderMap& headers) override;
    Envoy::Http::FilterDataStatus onUpdateData(absl::string_view host_address,
                                               Buffer::Instance& data, bool end_stream) override;

  private:
    bool isSSEResponse() const {
      return response_headers_ && response_headers_->ContentType() &&
             response_headers_->ContentType()->value().getStringView() == "text/event-stream";
    }
    absl::optional<std::string> upstream_address_;
    const EnvelopeSessionStateFactory& factory_;
    Envoy::Http::ResponseHeaderMap* response_headers_{nullptr};
    Buffer::OwnedImpl pending_chunk_;
  };

  EnvelopeSessionStateFactory(const EnvelopeSessionStateProto& config);

  Envoy::Http::SessionStatePtr create(Envoy::Http::RequestHeaderMap& headers) const override {
    absl::optional<std::string> mode;
    switch (mode_) {
    case envoy::extensions::http::stateful_session::envelope::v3::EnvelopeSessionState::kHeader:
      mode = parseAddressHeader(headers);
      break;
    case envoy::extensions::http::stateful_session::envelope::v3::EnvelopeSessionState::
        kSseEndpointMessage:
      mode = parseAddressSse(headers);
      break;
    default:
      ENVOY_LOG(error, "Unknown stateful session envelope mode");
      break;
    }
    return std::make_unique<SessionStateImpl>(mode, *this);
  }

private:
  absl::optional<std::string> parseAddressHeader(Envoy::Http::RequestHeaderMap& headers) const;
  absl::optional<std::string> parseAddressSse(Envoy::Http::RequestHeaderMap& headers) const;
  const envoy::extensions::http::stateful_session::envelope::v3::EnvelopeSessionState::
      SessionModeCase mode_;
  const Envoy::Http::LowerCaseString header_name_; // header mode
  const std::string param_name_;                   // data mode
};

} // namespace Envelope
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
