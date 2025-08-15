#include "contrib/mcp_sse_stateful_session/http/source/envelope.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpSseSessionState {
namespace Envelope {

void EnvelopeSessionStateFactory::SessionStateImpl::onUpdateHeader(
    absl::string_view host_address, Envoy::Http::ResponseHeaderMap& headers) {
  // Store response headers for SSE detection
  response_headers_ = &headers;
  UNREFERENCED_PARAMETER(host_address);
}

Envoy::Http::FilterDataStatus EnvelopeSessionStateFactory::SessionStateImpl::onUpdateData(
    absl::string_view host_address, Buffer::Instance& data, bool end_stream) {
  // Skip if not SSE response
  if (!isSSEResponse()) {
    return Envoy::Http::FilterDataStatus::Continue;
  }

  // Skip if session ID is already found
  if (session_id_found_) {
    return Envoy::Http::FilterDataStatus::Continue;
  }

  // check the pending chunk size
  // in case of wrong configuration on this filter
  if (pending_chunk_.length() + data.length() > factory_.max_pending_chunk_size_) {
    ENVOY_LOG(error, "Pending chunk size exceeds max pending chunk size: {}",
              pending_chunk_.length() + data.length());
    pending_chunk_.add(data);
    data.drain(data.length());
    data.move(pending_chunk_);
    session_id_found_ = true; // skip the rest of the data
    return Envoy::Http::FilterDataStatus::Continue;
  }

  // Append new data to pending buffer
  pending_chunk_.add(data);

  data.drain(data.length());

  while (pending_chunk_.length() > 0) {
    // Get pending chunk as string
    const std::string pending_chunk_str(
        static_cast<const char*>(pending_chunk_.linearize(pending_chunk_.length())),
        pending_chunk_.length());

    // Find next complete chunk
    size_t chunk_end_pos;
    size_t chunk_end_and_end_str;
    std::string chunk_end_string;

    for (const auto& chunk_end_pattern : factory_.chunk_end_patterns_) {
      if ((chunk_end_pos = pending_chunk_str.find(chunk_end_pattern)) != std::string::npos) {
        chunk_end_string = chunk_end_pattern;
        chunk_end_and_end_str = chunk_end_pos + chunk_end_pattern.length();
        break;
      }
    }

    if (chunk_end_string.empty()) {
      ENVOY_LOG(trace, "No complete chunk found, waiting for more data");
      break;
    }

    // Process current complete chunk
    Buffer::OwnedImpl chunk_buffer;
    chunk_buffer.add(pending_chunk_str.substr(0, chunk_end_pos));
    pending_chunk_.drain(chunk_end_and_end_str);

    const std::string chunk_buffer_str(
        static_cast<const char*>(chunk_buffer.linearize(chunk_buffer.length())),
        chunk_buffer.length());

    // Search for the parameter name in the URL
    const std::string param_name = factory_.param_name_;
    size_t param_pos = chunk_buffer_str.find(param_name + "=");
    if (param_pos != std::string::npos) {
      size_t value_start = param_pos + param_name.length() + 1;
      size_t value_end = chunk_buffer_str.find('&', value_start);
      if (value_end == std::string::npos) {
        value_end = chunk_buffer_str.length();
      }

      // Get original session ID
      const std::string original_session_id =
          chunk_buffer_str.substr(value_start, value_end - value_start);
      const char* host_address_c = host_address.data();
      uint64_t host_address_length = static_cast<uint64_t>(host_address.size());

      // Build new URL with encoded host address
      const std::string modified_url = absl::StrCat(
          chunk_buffer_str.substr(0, param_pos), param_name, "=", original_session_id,
          std::string(1, SEPARATOR), Envoy::Base64Url::encode(host_address_c, host_address_length),
          chunk_buffer_str.substr(value_end));

      data.add(modified_url);
      session_id_found_ = true;
    } else {
      // If parameter not found, keep chunk unchanged
      data.add(chunk_buffer);
    }

    // Add chunk ending
    data.add(chunk_end_string);
  }
  if (end_stream) {
    data.add(pending_chunk_);
    pending_chunk_.drain(pending_chunk_.length());
  }
  return Envoy::Http::FilterDataStatus::Continue;
}

EnvelopeSessionStateFactory::EnvelopeSessionStateFactory(const EnvelopeSessionStateProto& config)
    : param_name_(config.param_name()),
      chunk_end_patterns_(config.chunk_end_patterns().begin(), config.chunk_end_patterns().end()),
      max_pending_chunk_size_(config.max_pending_chunk_size()) {}

absl::optional<std::string>
EnvelopeSessionStateFactory::parseAddress(Envoy::Http::RequestHeaderMap& headers) const {
  const auto* path = headers.Path();
  if (!path) {
    return absl::nullopt;
  }

  // Parse query parameters
  const auto params =
      Envoy::Http::Utility::QueryParamsMulti::parseQueryString(path->value().getStringView())
          .data();
  auto it = params.find(param_name_);
  if (it == params.end() || it->second.empty()) {
    return absl::nullopt;
  }
  const std::string& session_value = it->second[0];
  ENVOY_LOG(debug, "Processing session value: {}", session_value);

  auto separator_pos = session_value.rfind(SEPARATOR);
  if (separator_pos == std::string::npos) {
    ENVOY_LOG(debug, "No separator found in session value: {}", session_value);
    return absl::nullopt;
  }

  std::string original_session_id = session_value.substr(0, separator_pos);
  std::string host_address = Envoy::Base64Url::decode(session_value.substr(separator_pos + 1));

  // Check if Base64Url decode was successful
  if (host_address.empty()) {
    ENVOY_LOG(debug, "Failed to decode host address from session value: {}", session_value);
    return absl::nullopt;
  }

  // Build new query
  std::string new_query;

  // First add the session ID parameter
  new_query += absl::StrCat(param_name_, "=", original_session_id);

  // Then append all other parameters
  for (const auto& param : params) {
    if (param.first == param_name_) {
      continue; // Skip the session ID as we already added it
    }
    for (const auto& value : param.second) {
      new_query += "&" + absl::StrCat(param.first, "=", value);
    }
  }

  const auto path_str = path->value().getStringView();
  auto query_start = path_str.find('?');
  std::string new_path = absl::StrCat(path_str.substr(0, query_start + 1), new_query);

  headers.setPath(new_path);
  ENVOY_LOG(debug, "Restored session ID: {}, host: {}", original_session_id, host_address);

  return host_address;
}

} // namespace Envelope
} // namespace McpSseSessionState
} // namespace Http
} // namespace Extensions
} // namespace Envoy
