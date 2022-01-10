#include "source/extensions/filters/http/ext_proc/mutation_utils.h"

#include "envoy/http/header_map.h"

#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

using Http::Headers;
using Http::LowerCaseString;

using envoy::config::core::v3::HeaderValueOption;
using envoy::service::ext_proc::v3::BodyMutation;
using envoy::service::ext_proc::v3::BodyResponse;
using envoy::service::ext_proc::v3::CommonResponse;
using envoy::service::ext_proc::v3::HeaderMutation;
using envoy::service::ext_proc::v3::HeadersResponse;

void MutationUtils::headersToProto(const Http::HeaderMap& headers_in,
                                   envoy::config::core::v3::HeaderMap& proto_out) {
  headers_in.iterate([&proto_out](const Http::HeaderEntry& e) -> Http::HeaderMap::Iterate {
    auto* new_header = proto_out.add_headers();
    new_header->set_key(std::string(e.key().getStringView()));
    new_header->set_value(std::string(e.value().getStringView()));
    return Http::HeaderMap::Iterate::Continue;
  });
}

void MutationUtils::applyCommonHeaderResponse(const HeadersResponse& response,
                                              Http::HeaderMap& headers) {
  if (response.has_response()) {
    const auto& common_response = response.response();
    if (common_response.has_header_mutation()) {
      applyHeaderMutations(common_response.header_mutation(), headers,
                           common_response.status() == CommonResponse::CONTINUE_AND_REPLACE);
    }
  }
}

void MutationUtils::applyHeaderMutations(const HeaderMutation& mutation, Http::HeaderMap& headers,
                                         bool replacing_message) {
  for (const auto& remove_header : mutation.remove_headers()) {
    if (Http::HeaderUtility::isRemovableHeader(remove_header)) {
      ENVOY_LOG(trace, "Removing header {}", remove_header);
      headers.remove(LowerCaseString(remove_header));
    } else {
      ENVOY_LOG(debug, "Header {} is not removable", remove_header);
    }
  }

  for (const auto& sh : mutation.set_headers()) {
    if (!sh.has_header()) {
      continue;
    }
    if (!isSettableHeader(sh, replacing_message)) {
      // Log the failure to set the header here, but don't log the value in case it's
      // something sensitive like the Authorization header.
      ENVOY_LOG(debug, "Ignorning improper attempt to set header {}", sh.header().key());
    } else {
      // Make "false" the default. This is logical and matches the ext_authz
      // filter. However, the router handles this same protobuf and uses "true"
      // as the default instead.
      const bool append = PROTOBUF_GET_WRAPPED_OR_DEFAULT(sh, append, false);
      const LowerCaseString lcKey(sh.header().key());
      if (append && !headers.get(lcKey).empty() && !isAppendableHeader(lcKey)) {
        ENVOY_LOG(debug, "Ignoring duplicate value for header {}", sh.header().key());
      } else {
        ENVOY_LOG(trace, "Setting header {} append = {}", sh.header().key(), append);
        if (append) {
          headers.addCopy(lcKey, sh.header().value());
        } else {
          headers.setCopy(lcKey, sh.header().value());
        }
      }
    }
  }
}

void MutationUtils::applyCommonBodyResponse(const BodyResponse& response,
                                            Http::RequestOrResponseHeaderMap* headers,
                                            Buffer::Instance& buffer) {
  if (response.has_response()) {
    const auto& common_response = response.response();
    if (headers != nullptr && common_response.has_header_mutation()) {
      applyHeaderMutations(common_response.header_mutation(), *headers,
                           common_response.status() == CommonResponse::CONTINUE_AND_REPLACE);
    }
    if (common_response.has_body_mutation()) {
      if (headers != nullptr) {
        // Always clear content length if we can before modifying body
        headers->removeContentLength();
      }
      applyBodyMutations(common_response.body_mutation(), buffer);
    }
  }
}

void MutationUtils::applyBodyMutations(const BodyMutation& mutation, Buffer::Instance& buffer) {
  switch (mutation.mutation_case()) {
  case BodyMutation::MutationCase::kClearBody:
    if (mutation.clear_body()) {
      ENVOY_LOG(trace, "Clearing HTTP body");
      buffer.drain(buffer.length());
    }
    break;
  case BodyMutation::MutationCase::kBody:
    ENVOY_LOG(trace, "Replacing body of {} bytes with new body of {} bytes", buffer.length(),
              mutation.body().size());
    buffer.drain(buffer.length());
    buffer.add(mutation.body());
    break;
  default:
    // Nothing to do on default
    break;
  }
}

bool MutationUtils::isValidHttpStatus(int code) { return (code >= 200); }

// Ignore attempts to set certain sensitive headers that can break later processing.
// We may re-enable some of these after further testing. This logic is specific
// to the ext_proc filter so it is not shared with HeaderUtils.
bool MutationUtils::isSettableHeader(const HeaderValueOption& header, bool replacing_message) {
  const auto& key = header.header().key();
  const auto& headers = Headers::get();
  if (absl::EqualsIgnoreCase(key, headers.HostLegacy.get()) ||
      absl::EqualsIgnoreCase(key, headers.Host.get()) ||
      (absl::EqualsIgnoreCase(key, headers.Method.get()) && !replacing_message) ||
      absl::EqualsIgnoreCase(key, headers.Scheme.get()) ||
      absl::StartsWithIgnoreCase(key, headers.prefix())) {
    return false;
  }
  if (absl::EqualsIgnoreCase(key, headers.Status.get())) {
    const auto& value = header.header().value();
    uint32_t status;
    if (!absl::SimpleAtoi(value, &status)) {
      ENVOY_LOG(debug, "Invalid value {} for HTTP status code", value);
      return false;
    }
    if (!isValidHttpStatus(status)) {
      ENVOY_LOG(debug, "Invalid HTTP status code {}", status);
      return false;
    }
  }
  return true;
}

// Ignore attempts to append a second value to any system header, as in general those
// were never designed to support multiple values.
bool MutationUtils::isAppendableHeader(absl::string_view key) {
  return !key.empty() && key[0] != ':';
}

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
