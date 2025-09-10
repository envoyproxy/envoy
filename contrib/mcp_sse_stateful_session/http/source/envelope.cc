#include "contrib/mcp_sse_stateful_session/http/source/envelope.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace SseSessionState {
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

  // Check the pending chunk size to prevent memory issues
  // in case of wrong configuration on this filter
  if (pending_chunk_.length() + data.length() > factory_.max_pending_chunk_size_) {
    ENVOY_LOG(error, "Pending chunk size exceeds max pending chunk size: {}",
              pending_chunk_.length() + data.length());
    pending_chunk_.move(data);
    data.move(pending_chunk_);
    session_id_found_ = true; // Skip the rest of the data
    return Envoy::Http::FilterDataStatus::Continue;
  }

  // Append new data to pending buffer
  pending_chunk_.move(data);

  while (pending_chunk_.length() > 0) {
    // Find next complete chunk by searching for chunk end patterns
    ssize_t chunk_end_pos = -1;
    size_t chunk_end_pattern_length = 0;
    const std::string* found_pattern = nullptr;

    // Search for the first occurrence of any chunk end pattern
    for (const auto& chunk_end_pattern : factory_.chunk_end_patterns_) {
      ssize_t pos =
          pending_chunk_.search(chunk_end_pattern.data(), chunk_end_pattern.length(), 0, 0);
      if (pos >= 0 && (chunk_end_pos == -1 || pos < chunk_end_pos)) {
        chunk_end_pos = pos;
        chunk_end_pattern_length = chunk_end_pattern.length();
        found_pattern = &chunk_end_pattern;
      }
    }

    if (chunk_end_pos == -1) {
      ENVOY_LOG(trace, "No complete chunk found, waiting for more data");
      break;
    }

    // Process current complete chunk
    Buffer::OwnedImpl chunk_buffer;
    // Move chunk content (excluding the end pattern) to avoid copying
    chunk_buffer.move(pending_chunk_, chunk_end_pos);
    pending_chunk_.drain(chunk_end_pattern_length);

    // Search for the parameter name in the chunk
    const std::string param_search = factory_.param_name_ + "=";
    ssize_t param_pos = chunk_buffer.search(param_search.data(), param_search.length(), 0, 0);

    if (param_pos >= 0) {
      // Found the parameter, extract its value
      size_t value_start = param_pos + param_search.length();

      // Search for the end of the parameter value (either '&' or end of string)
      const char ampersand = '&';
      ssize_t value_end = chunk_buffer.search(&ampersand, 1, value_start, 0);

      if (value_end == -1) {
        // No '&' found, parameter value extends to end of chunk
        value_end = chunk_buffer.length();
      }

      // Encode host address using Base64Url
      const char* host_address_c = host_address.data();
      uint64_t host_address_length = static_cast<uint64_t>(host_address.size());
      const std::string encoded_host =
          Envoy::Base64Url::encode(host_address_c, host_address_length);

      // Build modified URL by moving buffers and adding encoded host
      data.move(chunk_buffer, value_end);
      // Add separator and encoded host
      data.add(std::string(1, SEPARATOR));
      data.add(encoded_host);
      // Move suffix (after parameter value)
      data.move(chunk_buffer);

      session_id_found_ = true;
    } else {
      // Parameter not found, keep chunk unchanged
      data.move(chunk_buffer);
    }

    // Add chunk ending pattern
    data.add(*found_pattern);
  }

  if (end_stream) {
    data.move(pending_chunk_);
  }

  return Envoy::Http::FilterDataStatus::Continue;
}

EnvelopeSessionStateFactory::EnvelopeSessionStateFactory(const EnvelopeSessionStateProto& config)
    : param_name_(config.param_name()),
      chunk_end_patterns_(config.chunk_end_patterns().begin(), config.chunk_end_patterns().end()),
      max_pending_chunk_size_(config.max_pending_chunk_size() > 0 ? config.max_pending_chunk_size()
                                                                  : 4096) {
  ENVOY_LOG(debug, "max_pending_chunk_size: {}", max_pending_chunk_size_);
}

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
  // Estimate size to avoid multiple reallocations
  size_t estimated_size = param_name_.length() + 1 + original_session_id.length();
  for (const auto& param : params) {
    if (param.first != param_name_) {
      estimated_size += param.first.length() + 1; // "&" + param_name
      for (const auto& value : param.second) {
        estimated_size += value.length() + 1; // "=" + value
      }
    }
  }
  new_query.reserve(estimated_size);

  // First add the session ID parameter
  absl::StrAppend(&new_query, param_name_, "=", original_session_id);

  // Then append all other parameters
  for (const auto& param : params) {
    if (param.first == param_name_) {
      continue; // Skip the session ID as we already added it
    }
    for (const auto& value : param.second) {
      absl::StrAppend(&new_query, "&", param.first, "=", value);
    }
  }

  // Build final path
  const auto path_str = path->value().getStringView();
  auto query_start = path_str.find('?');
  std::string new_path;
  new_path.reserve(query_start + 1 + new_query.length());
  absl::StrAppend(&new_path, path_str.substr(0, query_start + 1), new_query);

  headers.setPath(new_path);
  ENVOY_LOG(debug, "Restored session ID: {}, host: {}", original_session_id, host_address);

  return host_address;
}

} // namespace Envelope
} // namespace SseSessionState
} // namespace Http
} // namespace Extensions
} // namespace Envoy
