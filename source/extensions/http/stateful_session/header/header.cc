#include "source/extensions/http/stateful_session/header/header.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Header {

constexpr absl::string_view OriginPartFlag = ";origin:";

void HeaderBasedSessionStateFactory::SessionStateImpl::onUpdate(
    absl::string_view host_address, Envoy::Http::ResponseHeaderMap& headers) {

  if (factory_.mode_ == HeaderBasedSessionStateProto::PACKAGES) {
    auto origin_header = headers.get(factory_.name_);
    if (origin_header.size() != 1) {
      ENVOY_LOG(debug, "Header {} not exist or occurs multiple times", factory_.name_);
      return;
    }

    const std::string new_header =
        absl::StrCat(host_address, OriginPartFlag, origin_header[0]->value().getStringView());
    const std::string encoded_new_header = Envoy::Base64::encode(new_header);

    headers.setReferenceKey(factory_.name_, encoded_new_header);
    return;
  }

  // Normal OVERRIDE mode.
  if (!upstream_address_.has_value() || host_address != upstream_address_.value()) {
    const std::string encoded_address =
        Envoy::Base64::encode(host_address.data(), host_address.length());
    headers.setReferenceKey(factory_.name_, encoded_address);
  }
}

HeaderBasedSessionStateFactory::HeaderBasedSessionStateFactory(
    const HeaderBasedSessionStateProto& config)
    : name_(config.name()), mode_(config.mode()) {
  if (config.name().empty()) {
    throw EnvoyException("Header name cannot be empty for header based stateful sessions");
  }
}

absl::optional<std::string>
HeaderBasedSessionStateFactory::parseAddress(Envoy::Http::RequestHeaderMap& headers) const {
  auto hdr = headers.get(Envoy::Http::LowerCaseString(name_));
  if (hdr.empty()) {
    return absl::nullopt;
  }

  std::string content = Envoy::Base64::decode(hdr[0]->value().getStringView());
  absl::string_view content_view(content);

  if (mode_ == HeaderBasedSessionStateProto::PACKAGES) {
    const auto origin_pos = content_view.find(OriginPartFlag);

    absl::string_view origin;
    if (origin_pos != absl::string_view::npos) {
      origin = content_view.substr(origin_pos + OriginPartFlag.size());
    }
    if (origin.empty()) {
      // Do nothing if the 'origin' part is not found or invalid.
      ENVOY_LOG(info, "Header {} does not contain valid 'origin' part", name_);
      return absl::nullopt;
    }

    headers.setReferenceKey(name_, origin);
    content_view = content_view.substr(0, origin_pos); // Remove the 'origin' part.
  }

  content.resize(content_view.size()); // The left part of content view is the address.
  return !content.empty() ? absl::make_optional(std::move(content)) : absl::nullopt;
}

} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
