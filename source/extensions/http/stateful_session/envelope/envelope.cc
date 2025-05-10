#include "source/extensions/http/stateful_session/envelope/envelope.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Envelope {

constexpr absl::string_view OriginUpstreamValuePartFlag = "UV:";

void EnvelopeSessionStateFactory::SessionStateImpl::onUpdate(
    absl::string_view host_address, Envoy::Http::ResponseHeaderMap& headers) {

  const auto upstream_value_header = headers.get(factory_.name_);
  if (upstream_value_header.size() != 1) {
    ENVOY_LOG(trace, "Header {} not exist or occurs multiple times", factory_.name_);
    return;
  }

  const std::string new_header =
      absl::StrCat(Envoy::Base64::encode(host_address), ";", OriginUpstreamValuePartFlag,
                   Envoy::Base64::encode(upstream_value_header[0]->value().getStringView()));
  headers.setReferenceKey(factory_.name_, new_header);
}

EnvelopeSessionStateFactory::EnvelopeSessionStateFactory(const EnvelopeSessionStateProto& config)
    : name_(config.header().name()) {}

absl::optional<std::string>
EnvelopeSessionStateFactory::parseAddress(Envoy::Http::RequestHeaderMap& headers) const {
  const auto hdr = headers.get(name_);
  if (hdr.empty()) {
    return absl::nullopt;
  }
  const absl::InlinedVector<absl::string_view, 2> parts =
      absl::StrSplit(hdr[0]->value().getStringView(), ';', absl::SkipEmpty());
  if (parts.empty()) {
    return absl::nullopt;
  }
  std::string upstream_host = Base64::decode(parts[0]);

  absl::string_view upstream_value;

  // parts[0] is the base64 encoded upstream address and should be skipped.
  const auto other_parts_view = absl::Span<const absl::string_view>(parts).subspan(1);
  for (absl::string_view part : other_parts_view) {
    if (absl::StartsWith(part, OriginUpstreamValuePartFlag)) {
      upstream_value = part.substr(OriginUpstreamValuePartFlag.size());
      break;
    }
  }

  const std::string decoded = Envoy::Base64::decode(upstream_value);
  if (decoded.empty()) {
    // Do nothing if the 'UV' part is not valid or if there is no UV part.
    ENVOY_LOG(info, "Header {} contains invalid 'UV' part or there is no 'UV' part", name_);
    return absl::nullopt;
  }
  headers.setReferenceKey(name_, decoded);

  return !upstream_host.empty() ? absl::make_optional(std::move(upstream_host)) : absl::nullopt;
}

} // namespace Envelope
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
