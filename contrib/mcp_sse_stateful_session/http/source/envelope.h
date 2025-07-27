#pragma once

#include <memory>

#include "envoy/http/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base64.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"

#include "contrib/envoy/extensions/http/mcp_sse_stateful_session/envelope/v3alpha/envelope.pb.h"
#include "contrib/mcp_sse_stateful_session/http/source/mcp_sse_stateful_session.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpSseSessionState {
namespace Envelope {

using EnvelopeSessionStateProto =
    envoy::extensions::http::mcp_sse_stateful_session::envelope::v3alpha::EnvelopeSessionState;

class EnvelopeSessionStateFactory : public McpSseSessionStateFactory,
                                      public Logger::Loggable<Logger::Id::http> {
  friend class SessionStateImpl;

public:
  class SessionStateImpl : public McpSseSessionState {
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

  McpSseSessionStatePtr create(Envoy::Http::RequestHeaderMap& headers) const override {
    absl::optional<std::string> address = parseAddress(headers);
    return std::make_unique<SessionStateImpl>(address, *this);
  }

private:
  absl::optional<std::string> parseAddress(Envoy::Http::RequestHeaderMap& headers) const;
  const std::string param_name_;
  static constexpr char SEPARATOR = '.'; // separate session ID and host address
};

} // namespace Envelope
} // namespace McpSseSessionState
} // namespace Http
} // namespace Extensions
} // namespace Envoy
