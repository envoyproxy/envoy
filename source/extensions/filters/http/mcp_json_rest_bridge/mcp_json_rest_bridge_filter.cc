#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

#include <array>

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/json_escape_string.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/mcp/constants.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/http_request_builder.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/trace_context.h"

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "fmt/format.h"
#include "utf8_validity.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::Envoy::Extensions::HttpFilters::McpJsonRestBridge::McpTraceContext;
using ::nlohmann::json;
namespace McpConstants = Envoy::Extensions::Filters::Common::Mcp::McpConstants;

constexpr uint32_t DEFAULT_MAX_REQUEST_BODY_SIZE = 1024 * 64;    // 64KB
constexpr uint32_t DEFAULT_MAX_RESPONSE_BODY_SIZE = 1024 * 1024; // 1MB

const Http::LowerCaseString& traceparentHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "traceparent");
}

const Http::LowerCaseString& tracestateHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "tracestate");
}

const Http::LowerCaseString& baggageHeader() {
  CONSTRUCT_ON_FIRST_USE(Http::LowerCaseString, "baggage");
}

bool isMcpProtocolVersionSupported(absl::string_view protocol_version) {
  static const absl::NoDestructor<absl::flat_hash_set<absl::string_view>> supported_mcp_versions({
      McpConstants::LATEST_SUPPORTED_MCP_VERSION,
      McpConstants::FALLBACK_PROTOCOL_VERSION,
      McpConstants::MCP_VERSION_2024_11_05,
      McpConstants::MCP_VERSION_2025_06_18,
  });
  return supported_mcp_versions->contains(protocol_version);
}

absl::StatusOr<json> getSessionId(const json& json_rpc) {
  if (auto it = json_rpc.find(McpConstants::ID_FIELD); it != json_rpc.end()) {
    if (it->is_number_integer() || it->is_string()) {
      return *it;
    }
    return absl::InvalidArgumentError("JSON-RPC request ID is not an integer or a string.");
  }
  return absl::InvalidArgumentError("JSON-RPC request (except notification) does not have an ID.");
}

json translateJsonRestResponseToJsonRpc(absl::string_view tool_call_response,
                                        const json& session_id, bool is_error) {
  return json{
      {McpConstants::JSONRPC_FIELD, McpConstants::JSONRPC_VERSION},
      {McpConstants::ID_FIELD, session_id},
      {McpConstants::RESULT_FIELD,
       {
           {McpConstants::CONTENT_FIELD,
            json::array({{{McpConstants::TYPE_FIELD, McpConstants::TEXT_FIELD},
                          {McpConstants::TEXT_FIELD, tool_call_response}}})},
           {McpConstants::IS_ERROR_FIELD, is_error},
       }},
  };
}

json generateInitializeResponse(const json& session_id, absl::string_view server_name,
                                absl::string_view protocol_version) {
  absl::string_view negotiated_protocol_version = McpConstants::LATEST_SUPPORTED_MCP_VERSION;
  if (isMcpProtocolVersionSupported(protocol_version)) {
    negotiated_protocol_version = protocol_version;
  }

  json ret;
  ret[McpConstants::JSONRPC_FIELD] = McpConstants::JSONRPC_VERSION;
  ret[McpConstants::ID_FIELD] = session_id;

  json result;
  result[McpConstants::PROTOCOL_VERSION_FIELD] = negotiated_protocol_version;
  // TODO(guoyilin42): Support list_changed from ServerToolConfig and description from ServerInfo.
  result[McpConstants::CAPABILITIES_FIELD][McpConstants::TOOLS_FIELD]
        [McpConstants::LIST_CHANGED_FIELD] = false;
  result[McpConstants::SERVER_INFO_FIELD][McpConstants::NAME_FIELD] = server_name;
  result[McpConstants::SERVER_INFO_FIELD][McpConstants::VERSION_FIELD] =
      McpConstants::DEFAULT_SERVER_VERSION;
  ret[McpConstants::RESULT_FIELD] = result;
  return ret;
}

json generateErrorJsonResponse(int error_code, absl::string_view error_message) {
  return json{
      {McpConstants::ERROR_CODE_FIELD, error_code},
      {McpConstants::ERROR_MESSAGE_FIELD, error_message},
  };
}

int getResponseCode(Http::ResponseHeaderMapOptConstRef response_headers) {
  if (!response_headers.has_value()) {
    return static_cast<int>(Http::Code::InternalServerError);
  }
  int status_code;
  if (!absl::SimpleAtoi(response_headers->getStatusValue(), &status_code)) {
    return static_cast<int>(Http::Code::InternalServerError);
  }
  return status_code;
}

bool validateRequestMcpVersion(absl::string_view method,
                               Http::RequestHeaderMapOptConstRef request_headers,
                               absl::string_view fallback_protocol_version) {
  static const absl::NoDestructor<Http::LowerCaseString> mcp_protocol_version_header(
      McpConstants::MCP_PROTOCOL_VERSION_HEADER);
  // The initialize request is not expected to have MCP protocol version header.
  // So we will not check the protocol version for this request.
  if (method == McpConstants::Methods::INITIALIZE) {
    return true;
  }
  absl::string_view protocol_version = fallback_protocol_version;
  if (request_headers.has_value()) {
    auto headers = request_headers->get(*mcp_protocol_version_header);
    if (!headers.empty()) {
      protocol_version = headers[0]->value().getStringView();
    }
  }
  return isMcpProtocolVersionSupported(protocol_version);
}

void setTraceContextHeaders(Http::RequestHeaderMap& request_headers,
                            const McpTraceContext& trace_context) {
  if (!trace_context.traceparent().empty()) {
    request_headers.setCopy(traceparentHeader(), trace_context.traceparent());
    if (trace_context.tracestate().empty()) {
      request_headers.remove(tracestateHeader());
    } else {
      request_headers.setCopy(tracestateHeader(), trace_context.tracestate());
    }
  }

  if (!trace_context.baggage().empty()) {
    request_headers.setCopy(baggageHeader(), trace_context.baggage());
  }
}

std::array<EndpointKey, 4> getEndpointLookupKeys(absl::string_view host, absl::string_view path) {
  return {{
      {std::string(host), std::string(path)},
      {std::string(host), ""},
      {"", std::string(path)},
      {"", ""},
  }};
}

} // namespace

absl::string_view bridgeStatusToString(BridgeStatus status) {
  switch (status) {
  case BridgeStatus::Ok:
    return BridgeStatusValues::OK;
  case BridgeStatus::RequestNotPost:
    return BridgeStatusValues::REQUEST_NOT_POST;
  case BridgeStatus::RequestTooLarge:
    return BridgeStatusValues::REQUEST_TOO_LARGE;
  case BridgeStatus::RequestParseError:
    return BridgeStatusValues::REQUEST_PARSE_ERROR;
  case BridgeStatus::RequestUnsupportedProtocolVersion:
    return BridgeStatusValues::REQUEST_UNSUPPORTED_PROTOCOL_VERSION;
  case BridgeStatus::RequestInitializeNotValid:
    return BridgeStatusValues::REQUEST_INITIALIZE_NOT_VALID;
  case BridgeStatus::RequestMethodNotSupported:
    return BridgeStatusValues::REQUEST_METHOD_NOT_SUPPORTED;
  case BridgeStatus::RequestMethodNotFound:
    return BridgeStatusValues::REQUEST_METHOD_NOT_FOUND;
  case BridgeStatus::RequestMethodNotString:
    return BridgeStatusValues::REQUEST_METHOD_NOT_STRING;
  case BridgeStatus::RequestIdNotFound:
    return BridgeStatusValues::REQUEST_ID_NOT_FOUND;
  case BridgeStatus::RequestToolParamsNotFound:
    return BridgeStatusValues::REQUEST_TOOL_PARAMS_NOT_FOUND;
  case BridgeStatus::RequestToolNameNotFound:
    return BridgeStatusValues::REQUEST_TOOL_NAME_NOT_FOUND;
  case BridgeStatus::RequestUnknownTool:
    return BridgeStatusValues::REQUEST_UNKNOWN_TOOL;
  case BridgeStatus::RequestToolArgumentsInvalid:
    return BridgeStatusValues::REQUEST_TOOL_ARGUMENTS_INVALID;
  case BridgeStatus::RequestToolTranscodingFailure:
    return BridgeStatusValues::REQUEST_TOOL_TRANSCODING_FAILURE;
  case BridgeStatus::RequestPassthrough:
    return BridgeStatusValues::REQUEST_PASSTHROUGH;
  case BridgeStatus::ResponseTooLarge:
    return BridgeStatusValues::RESPONSE_TOO_LARGE;
  case BridgeStatus::ResponseInvalidUtf8:
    return BridgeStatusValues::RESPONSE_INVALID_UTF8;
  case BridgeStatus::ResponseBackendError:
    return BridgeStatusValues::RESPONSE_BACKEND_ERROR;
  case BridgeStatus::ResponseParseError:
    return BridgeStatusValues::RESPONSE_PARSE_ERROR;
  }
  return "UNKNOWN";
}

McpJsonRestBridgeFilterConfig::McpJsonRestBridgeFilterConfig(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
        proto_config)
    : proto_config_(proto_config), fallback_protocol_version_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(
                                       proto_config_.server_info(), fallback_protocol_version,
                                       std::string(McpConstants::FALLBACK_PROTOCOL_VERSION))),
      max_request_body_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config_, max_request_body_size,
                                                             DEFAULT_MAX_REQUEST_BODY_SIZE)),
      max_response_body_size_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config_, max_response_body_size,
                                                              DEFAULT_MAX_RESPONSE_BODY_SIZE)),
      clear_route_cache_(!proto_config_.disable_clear_route_cache()) {
  const auto& tool_config = proto_config_.tool_config();
  std::string host = tool_config.default_server_info().host();
  std::string path = tool_config.default_server_info().path();
  if (host.empty() && path.empty()) {
    path = "/mcp";
  }
  EndpointKey key{host, path};
  auto& endpoint_config = endpoint_configs_[key];
  for (const auto& tool : tool_config.tools()) {
    if (endpoint_config.tool_entries
            .try_emplace(tool.name(),
                         ToolEntry{tool.http_rule(), tool.text_content_streaming_enabled(), &tool})
            .second) {
      endpoint_config.tools.push_back(&tool);
    }
  }
  if (tool_config.has_tool_list_http_rule()) {
    endpoint_config.tool_list_http_rule = tool_config.tool_list_http_rule();
  }
  endpoint_config.tool_list_local = tool_config.has_tool_list_local();

  ENVOY_LOG(debug, "Received MCP JSON REST Bridge config: {}", proto_config_.DebugString());
}

absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
McpJsonRestBridgeFilterConfig::getHttpRule(absl::string_view tool_name, absl::string_view host,
                                           absl::string_view path) const {
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    auto it = endpoint_configs_.find(key);
    if (it != endpoint_configs_.end()) {
      auto tool_it = it->second.tool_entries.find(tool_name);
      if (tool_it != it->second.tool_entries.end()) {
        return tool_it->second.http_rule;
      }
    }
  }
  return absl::InvalidArgumentError(
      fmt::format("Failed to find http rule for tool_name: {}", tool_name));
}

bool McpJsonRestBridgeFilterConfig::textContentStreamingEnabled(absl::string_view tool_name,
                                                                absl::string_view host,
                                                                absl::string_view path) const {
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    auto it = endpoint_configs_.find(key);
    if (it != endpoint_configs_.end()) {
      auto tool_it = it->second.tool_entries.find(tool_name);
      if (tool_it != it->second.tool_entries.end()) {
        return tool_it->second.text_content_streaming_enabled;
      }
    }
  }
  return false;
}

absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
McpJsonRestBridgeFilterConfig::getToolsListHttpRule(absl::string_view host,
                                                    absl::string_view path) const {
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    auto it = endpoint_configs_.find(key);
    if (it != endpoint_configs_.end()) {
      if (it->second.tool_list_http_rule.has_value()) {
        return *it->second.tool_list_http_rule;
      }
    }
  }
  return absl::NotFoundError("tools_list_http_rule is not configured.");
}

std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>
McpJsonRestBridgeFilterConfig::toolListLocalTools(absl::string_view host,
                                                  absl::string_view path) const {
  std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*> tools;
  absl::flat_hash_set<absl::string_view> added_tools;
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    auto it = endpoint_configs_.find(key);
    if (it != endpoint_configs_.end() && it->second.tool_list_local) {
      for (const auto* tool : it->second.tools) {
        if (added_tools.insert(tool->name()).second) {
          tools.push_back(tool);
        }
      }
    }
  }
  return tools;
}

bool McpJsonRestBridgeFilterConfig::toolListLocal(absl::string_view host,
                                                  absl::string_view path) const {
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    auto it = endpoint_configs_.find(key);
    if (it != endpoint_configs_.end()) {
      if (it->second.tool_list_local) {
        return true;
      }
    }
  }
  return false;
}

bool McpJsonRestBridgeFilterConfig::hasEndpoint(absl::string_view host,
                                                absl::string_view path) const {
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    if (endpoint_configs_.contains(key)) {
      return true;
    }
  }
  return false;
}

McpJsonRestBridgePerRouteConfig::McpJsonRestBridgePerRouteConfig(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute&
        proto_config)
    : proto_config_(proto_config) {
  if (proto_config_.tool_config().empty()) {
    EndpointKey key{"", "/mcp"};
    endpoint_configs_.try_emplace(key, EndpointConfig());
  } else {
    for (const auto& tool_config : proto_config_.tool_config()) {
      std::string host = tool_config.default_server_info().host();
      std::string path = tool_config.default_server_info().path();
      if (host.empty() && path.empty()) {
        path = "/mcp";
      }
      EndpointKey key{host, path};
      auto& endpoint_config = endpoint_configs_[key];
      for (const auto& tool : tool_config.tools()) {
        if (endpoint_config.tool_entries
                .try_emplace(tool.name(), ToolEntry{tool.http_rule(),
                                                    tool.text_content_streaming_enabled(), &tool})
                .second) {
          endpoint_config.tools.push_back(&tool);
        }
      }
      if (!endpoint_config.tool_list_http_rule.has_value() &&
          tool_config.has_tool_list_http_rule()) {
        endpoint_config.tool_list_http_rule = tool_config.tool_list_http_rule();
      }
      if (tool_config.has_tool_list_local()) {
        endpoint_config.tool_list_local = true;
      }
    }
  }
  ENVOY_LOG(debug, "Received MCP JSON REST Bridge per-route config: {}",
            proto_config_.DebugString());
}

absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
McpJsonRestBridgePerRouteConfig::getHttpRule(absl::string_view tool_name, absl::string_view host,
                                             absl::string_view path) const {
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    auto it = endpoint_configs_.find(key);
    if (it != endpoint_configs_.end()) {
      auto tool_it = it->second.tool_entries.find(tool_name);
      if (tool_it != it->second.tool_entries.end()) {
        return tool_it->second.http_rule;
      }
    }
  }
  return absl::InvalidArgumentError(
      fmt::format("Failed to find http rule for tool_name: {}", tool_name));
}

bool McpJsonRestBridgePerRouteConfig::textContentStreamingEnabled(absl::string_view tool_name,
                                                                  absl::string_view host,
                                                                  absl::string_view path) const {
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    auto it = endpoint_configs_.find(key);
    if (it != endpoint_configs_.end()) {
      auto tool_it = it->second.tool_entries.find(tool_name);
      if (tool_it != it->second.tool_entries.end()) {
        return tool_it->second.text_content_streaming_enabled;
      }
    }
  }
  return false;
}

absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
McpJsonRestBridgePerRouteConfig::getToolsListHttpRule(absl::string_view host,
                                                      absl::string_view path) const {
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    auto it = endpoint_configs_.find(key);
    if (it != endpoint_configs_.end()) {
      if (it->second.tool_list_http_rule.has_value()) {
        return *it->second.tool_list_http_rule;
      }
    }
  }
  return absl::NotFoundError("tools_list_http_rule is not configured.");
}

std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>
McpJsonRestBridgePerRouteConfig::toolListLocalTools(absl::string_view host,
                                                    absl::string_view path) const {
  std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*> tools;
  absl::flat_hash_set<absl::string_view> added_tools;
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    auto it = endpoint_configs_.find(key);
    if (it != endpoint_configs_.end() && it->second.tool_list_local) {
      for (const auto* tool : it->second.tools) {
        if (added_tools.insert(tool->name()).second) {
          tools.push_back(tool);
        }
      }
    }
  }
  return tools;
}

bool McpJsonRestBridgePerRouteConfig::toolListLocal(absl::string_view host,
                                                    absl::string_view path) const {
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    auto it = endpoint_configs_.find(key);
    if (it != endpoint_configs_.end()) {
      if (it->second.tool_list_local) {
        return true;
      }
    }
  }
  return false;
}

bool McpJsonRestBridgePerRouteConfig::hasEndpoint(absl::string_view host,
                                                  absl::string_view path) const {
  for (const auto& key : getEndpointLookupKeys(host, path)) {
    if (endpoint_configs_.contains(key)) {
      return true;
    }
  }
  return false;
}

Http::FilterHeadersStatus
McpJsonRestBridgeFilter::decodeHeaders(Http::RequestHeaderMap& request_headers, bool) {
  absl::string_view path = request_headers.getPathValue();
  auto query_idx = path.find('?');
  if (query_idx != absl::string_view::npos) {
    path = path.substr(0, query_idx);
  }

  std::string server_name =
      std::string(Http::Utility::parseAuthority(request_headers.getHostValue()).host_);

  const auto* per_route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<McpJsonRestBridgePerRouteConfig>(
          decoder_callbacks_);
  bool has_endpoint = false;
  if (per_route_config != nullptr) {
    has_endpoint = per_route_config->hasEndpoint(server_name, path);
  } else {
    has_endpoint = config_->hasEndpoint(server_name, path);
  }

  if (!has_endpoint) {
    mcp_operation_ = McpOperation::Unspecified;
    return Http::FilterHeadersStatus::Continue;
  }

  path_ = std::string(path);
  mcp_operation_ = McpOperation::Undecided;
  server_name_ = std::move(server_name);

  if (request_headers.getMethodValue() != Http::Headers::get().MethodValues.Post) {
    ENVOY_STREAM_LOG(warn, "Only POST method is supported for MCP. Received: {}",
                     *decoder_callbacks_, request_headers.getMethodValue());
    sendErrorResponse(
        Http::Code::MethodNotAllowed, BridgeStatus::RequestNotPost, "Method Not Allowed",
        [](Http::ResponseHeaderMap& response_headers) {
          response_headers.addCopy(Http::LowerCaseString("allow"),
                                   Http::Headers::get().MethodValues.Post);
        },
        "", json::object(), Grpc::Status::WellKnownGrpcStatus::InvalidArgument);
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus McpJsonRestBridgeFilter::decodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (mcp_operation_ == McpOperation::Unspecified) {
    return Http::FilterDataStatus::Continue;
  }

  const auto* per_route_config =
      Http::Utility::resolveMostSpecificPerFilterConfig<McpJsonRestBridgePerRouteConfig>(
          decoder_callbacks_);

  const uint32_t max_request_body_size = config_->maxRequestBodySize();
  if (max_request_body_size > 0 &&
      (request_body_.length() + data.length()) > max_request_body_size) {
    ENVOY_STREAM_LOG(error, "Request body exceeds limit. Size: {}, Limit: {}", *decoder_callbacks_,
                     request_body_.length() + data.length(), max_request_body_size);
    sendErrorResponse(Http::Code::PayloadTooLarge, BridgeStatus::RequestTooLarge,
                      generateErrorJsonResponse(-32000, "Request body too large").dump());
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  request_body_.move(data);

  if (!end_stream) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  const size_t total_size = request_body_.length();
  void* linearized_data = request_body_.linearize(total_size);
  const char* json_ptr = static_cast<const char*>(linearized_data);
  json request_body_json = json::parse(json_ptr, json_ptr + total_size,
                                       /*parser_callback_t=*/nullptr, /*allow_exceptions=*/false);

  if (request_body_json.is_discarded()) {
    ENVOY_STREAM_LOG(error, "Failed to parse JSON-RPC request body.", *decoder_callbacks_);
    sendErrorResponse(Http::Code::BadRequest, BridgeStatus::RequestParseError,
                      generateErrorJsonResponse(-32700, "JSON parse error").dump());
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  handleMcpMethod(request_body_json, decoder_callbacks_->requestHeaders(), per_route_config);
  data.add(request_body_str_);
  request_body_str_.clear();

  if (mcp_operation_ == McpOperation::Initialization ||
      mcp_operation_ == McpOperation::InitializationAck ||
      mcp_operation_ == McpOperation::OperationFailed ||
      mcp_operation_ == McpOperation::ToolsListLocal) {
    // sendLocalReply/encodeHeaders was called in handleMcpMethod for these operations.
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus
McpJsonRestBridgeFilter::encodeHeaders(Http::ResponseHeaderMap& response_headers, bool end_stream) {
  switch (mcp_operation_) {
  case McpOperation::Unspecified:
  case McpOperation::Undecided:
  case McpOperation::Initialization:
  // The response for InitializedNotification is empty body so we don't need
  // to modify the response headers.
  case McpOperation::InitializationAck:
  // ToolsListLocal sends a local reply, so the headers are already correct.
  case McpOperation::ToolsListLocal:
    return Http::FilterHeadersStatus::Continue;
  default:
    break;
  }

  // Streaming mode: pre-build the JSON-RPC prefix/suffix, strip Content-Length
  // (final size is unknown), and let the headers flow through immediately so
  // the client can start receiving data without waiting for the full body.
  if (mcp_operation_ == McpOperation::ToolsCall && text_content_streaming_enabled_) {
    buildStreamingPrefixAndSuffix(getResponseCode(response_headers) >=
                                  static_cast<int>(Http::Code::BadRequest));
    response_headers.removeContentLength();
    response_headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
    return Http::FilterHeadersStatus::Continue;
  }

  // TODO(guoyilin42): Handle headers-only upstream responses (e.g., 204 No Content).
  // Currently, these cases bypass transcoding, which can cause MCP SDKs to timeout
  // or throw exceptions because they expect a valid JSON-RPC response with a
  // matching ID. Envoy should generate a synthetic JSON-RPC response (e.g., an
  // empty ToolResult or a generic error) to ensure client stability.
  return end_stream ? Http::FilterHeadersStatus::Continue
                    : Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus McpJsonRestBridgeFilter::encodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  // No need to encode the response body for Initialization and InitializationAck. ToolsListLocal is
  // a local response, and the response body is already encoded.
  if (mcp_operation_ == McpOperation::Unspecified ||
      mcp_operation_ == McpOperation::Initialization ||
      mcp_operation_ == McpOperation::InitializationAck ||
      mcp_operation_ == McpOperation::ToolsListLocal) {
    return Http::FilterDataStatus::Continue;
  }

  // Streaming fast-path for tools/call: JSON-escape each chunk on-the-fly without
  // buffering the full response body.
  if (!streaming_json_prefix_.empty()) {
    uint64_t len = data.length();
    // Note: An empty chunk can arrive when the upstream uses the body + trailer pattern (end_stream
    // is false on the last data frame). It is a no-op here; the suffix will be appended in
    // encodeTrailers.
    absl::string_view chunk(static_cast<const char*>(data.linearize(len)), len);
    // TODO(guoyilin42): Consider adding text/event-stream backend response support and explore if
    // it needs buffering.
    std::string escaped_chunk = JsonEscaper::escapeString(chunk, JsonEscaper::extraSpace(chunk));

    data.drain(len);
    // Note: UTF-8 structural validation (i.e., utf8_range::IsStructurallyValid) is omitted
    // in the streaming fast-path due to the stateless nature of chunk processing (which lacks
    // a stateful UTF-8 validator to track multi-byte character boundaries across chunk limits).
    // If the upstream backend returns invalid UTF-8, it will be streamed to the client as-is,
    // which may cause the client to fail parsing the final JSON.
    if (is_first_streaming_chunk_) {
      ENVOY_STREAM_LOG(debug,
                       "Streaming: emitting prefix + first chunk ({} raw bytes, {} escaped bytes).",
                       *encoder_callbacks_, len, escaped_chunk.size());
      data.add(streaming_json_prefix_);
      is_first_streaming_chunk_ = false;
    } else {
      ENVOY_STREAM_LOG(debug, "Streaming: forwarding chunk ({} raw bytes, {} escaped bytes).",
                       *encoder_callbacks_, len, escaped_chunk.size());
    }
    data.add(escaped_chunk);
    if (end_stream) {
      ENVOY_STREAM_LOG(debug, "Streaming: appending suffix, stream complete.", *encoder_callbacks_);
      data.add(streaming_json_suffix_);
    }
    return Http::FilterDataStatus::Continue;
  }

  const uint32_t max_response_body_size = config_->maxResponseBodySize();
  if (max_response_body_size > 0 &&
      (response_body_.length() + data.length()) > max_response_body_size) {
    ENVOY_STREAM_LOG(error, "Response body exceeds limit. Size: {}, Limit: {}", *encoder_callbacks_,
                     response_body_.length() + data.length(), max_response_body_size);
    json error_json = {
        {McpConstants::JSONRPC_FIELD, McpConstants::JSONRPC_VERSION},
        // If the ID is missing in the request, the ID in the response should be null.
        {McpConstants::ID_FIELD, session_id_.has_value() ? *session_id_ : json(nullptr)},
        {McpConstants::ERROR_FIELD, generateErrorJsonResponse(-32000, "Response body too large")}};
    setResponseMetadata(BridgeStatus::ResponseTooLarge,
                        getResponseCode(encoder_callbacks_->responseHeaders()));
    mcp_operation_ = McpOperation::OperationFailed;
    decoder_callbacks_->sendLocalReply(
        Http::Code::InternalServerError, error_json.dump(),
        [](Http::ResponseHeaderMap& headers) {
          headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
        },
        Grpc::Status::WellKnownGrpcStatus::Internal,
        bridgeStatusToString(BridgeStatus::ResponseTooLarge));
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  response_body_.move(data);

  if (!end_stream) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  encodeJsonRpcData(encoder_callbacks_->responseHeaders());
  data.add(response_body_str_);
  response_body_str_.clear();
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus McpJsonRestBridgeFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  if (mcp_operation_ != McpOperation::ToolsList && mcp_operation_ != McpOperation::ToolsCall) {
    return Http::FilterTrailersStatus::Continue;
  }

  if (!streaming_json_prefix_.empty()) {
    ENVOY_STREAM_LOG(debug, "Streaming: appending suffix in encodeTrailers.", *encoder_callbacks_);
    Buffer::OwnedImpl data;
    if (is_first_streaming_chunk_) {
      data.add(streaming_json_prefix_);
      is_first_streaming_chunk_ = false;
    }
    data.add(streaming_json_suffix_);
    encoder_callbacks_->addEncodedData(data, false);
  } else {
    encodeJsonRpcData(encoder_callbacks_->responseHeaders());
    Buffer::OwnedImpl data(response_body_str_);
    response_body_str_.clear();
    encoder_callbacks_->addEncodedData(data, false);
  }

  return Http::FilterTrailersStatus::Continue;
}

void McpJsonRestBridgeFilter::buildStreamingPrefixAndSuffix(bool is_error) {
  // Build a reference JSON-RPC envelope with an empty text placeholder.
  json ref = {
      {McpConstants::JSONRPC_FIELD, McpConstants::JSONRPC_VERSION},
      {McpConstants::ID_FIELD, *session_id_},
      {McpConstants::RESULT_FIELD,
       {
           {McpConstants::CONTENT_FIELD,
            json::array({{{McpConstants::TYPE_FIELD, McpConstants::TEXT_FIELD},
                          {McpConstants::TEXT_FIELD, ""}}})},
           {McpConstants::IS_ERROR_FIELD, is_error},
       }},
  };
  std::string ref_json = ref.dump();

  // Locate the empty-string placeholder for the text value: `"text":""`.
  std::string marker = absl::StrCat("\"", McpConstants::TEXT_FIELD, "\":\"\"");
  size_t pos = ref_json.find(marker);
  if (pos == std::string::npos) {
    IS_ENVOY_BUG("JSON-RPC streaming marker not found in serialized envelope");
    return;
  }
  streaming_json_prefix_ = ref_json.substr(0, pos + marker.size() - 1);
  streaming_json_suffix_ = ref_json.substr(pos + marker.size() - 1);
}

// Send a local reply for a tools/list call. Construct the response body directly instead of
// building a JSON object, to minimize copying.
void McpJsonRestBridgeFilter::serveToolsListLocal(
    const nlohmann::json& json_rpc,
    const std::vector<
        const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>& tools) {
  std::string request_id_json = "null";
  if (json_rpc.contains("id")) {
    request_id_json = json_rpc["id"].dump();
  } else {
    ASSERT(false, "serveToolsListLocal requires an RPC ID");
  }

  size_t reserve_size = sizeof("{\"jsonrpc\":\"2.0\",\"id\":") - 1 + request_id_json.size() +
                        sizeof(",\"result\":{\"tools\":[") - 1 + sizeof("]}}") - 1;

  for (const auto* tool : tools) {
    reserve_size += sizeof("{\"name\":") - 1 + nlohmann::json(tool->name()).dump().size();

    if (!tool->tool_list_config().title().empty()) {
      reserve_size += sizeof(",\"title\":") - 1 +
                      nlohmann::json(tool->tool_list_config().title()).dump().size();
    }

    reserve_size += sizeof(",\"description\":") - 1 +
                    nlohmann::json(tool->tool_list_config().description()).dump().size() +
                    sizeof(",\"inputSchema\":") - 1;

    if (!tool->tool_list_config().input_schema().empty()) {
      reserve_size += tool->tool_list_config().input_schema().size();
    } else {
      reserve_size += sizeof("{\"type\":\"object\"}") - 1;
    }
    reserve_size += sizeof("}") - 1 + sizeof(",") - 1;
  }

  std::string response_data;
  response_data.reserve(reserve_size);
  absl::StrAppend(&response_data, "{\"jsonrpc\":\"2.0\",\"id\":", request_id_json,
                  ",\"result\":{\"tools\":[");

  bool first_tool = true;
  for (const auto* tool : tools) {
    if (!first_tool) {
      absl::StrAppend(&response_data, ",");
    }
    first_tool = false;

    absl::StrAppend(&response_data, "{\"name\":", nlohmann::json(tool->name()).dump());

    if (!tool->tool_list_config().title().empty()) {
      absl::StrAppend(&response_data,
                      ",\"title\":", nlohmann::json(tool->tool_list_config().title()).dump());
    }

    absl::StrAppend(&response_data, ",\"description\":",
                    nlohmann::json(tool->tool_list_config().description()).dump(),
                    ",\"inputSchema\":");

    // WARNING: assumes input_schema is trusted to be a valid JSON fragment. Does not validate.
    if (!tool->tool_list_config().input_schema().empty()) {
      absl::StrAppend(&response_data, tool->tool_list_config().input_schema());
    } else {
      absl::StrAppend(&response_data, "{\"type\":\"object\"}");
    }

    absl::StrAppend(&response_data, "}");
  }

  absl::StrAppend(&response_data, "]}}");

  decoder_callbacks_->sendLocalReply(
      Http::Code::OK, response_data,
      [](Http::ResponseHeaderMap& headers) {
        headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
      },
      Grpc::Status::WellKnownGrpcStatus::Ok, "mcp_json_rest_bridge_tools_list");
}

void McpJsonRestBridgeFilter::handleMcpMethod(
    const nlohmann::json& json_rpc, Http::RequestHeaderMapOptRef request_headers,
    const McpJsonRestBridgePerRouteConfig* per_route_config) {
  ENVOY_STREAM_LOG(debug, "Handling MCP JSON-RPC: {}", *decoder_callbacks_, json_rpc.dump());
  if (!validateJsonRpcIdAndMethod(json_rpc).ok()) {
    return;
  }

  std::string method = json_rpc[McpConstants::METHOD_FIELD];
  if (!validateRequestMcpVersion(method, request_headers, config_->fallbackProtocolVersion())) {
    sendErrorResponse(
        Http::Code::BadRequest, BridgeStatus::RequestUnsupportedProtocolVersion,
        generateErrorJsonResponse(-32602, "Unsupported protocol version").dump(), nullptr, method,
        json_rpc.contains(McpConstants::PARAMS_FIELD) ? json_rpc[McpConstants::PARAMS_FIELD]
                                                      : json::object());
    return;
  }

  if (method == McpConstants::Methods::TOOLS_LIST) {
    std::vector<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::ToolConfig*>
        tool_list_local_tools = (per_route_config == nullptr)
                                    ? config_->toolListLocalTools(server_name_, path_)
                                    : per_route_config->toolListLocalTools(server_name_, path_);
    absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule> http_rule =
        (per_route_config == nullptr) ? config_->getToolsListHttpRule(server_name_, path_)
                                      : per_route_config->getToolsListHttpRule(server_name_, path_);
    if (http_rule.ok() && !http_rule->get().empty()) {
      mcp_operation_ = McpOperation::ToolsList;
      // We don't support pagination for the tools/list request for now.
      if (request_headers.has_value()) {
        request_headers->setPath(http_rule->get());
        request_headers->setMethod(Http::Headers::get().MethodValues.Get);
        request_headers->removeTransferEncoding();
        request_headers->removeContentLength();
        request_headers->removeContentType();
        // Set AcceptEncoding to "identity" to prevent server encoding the response.
        request_headers->setCopy(Http::CustomHeaders::get().AcceptEncoding,
                                 Http::CustomHeaders::get().AcceptEncodingValues.Identity);
      }

      if (config_->clearRouteCache() && decoder_callbacks_->downstreamCallbacks().has_value()) {
        decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
      }
      setParsingMetadata(method, json_rpc.contains(McpConstants::PARAMS_FIELD)
                                     ? json_rpc[McpConstants::PARAMS_FIELD]
                                     : json::object());
      return;
    } else {
      bool tool_list_local = (per_route_config == nullptr)
                                 ? config_->toolListLocal(server_name_, path_)
                                 : per_route_config->toolListLocal(server_name_, path_);
      if (tool_list_local) {
        mcp_operation_ = McpOperation::ToolsListLocal;
        serveToolsListLocal(json_rpc, tool_list_local_tools);
        setParsingMetadata(method, json_rpc.contains(McpConstants::PARAMS_FIELD)
                                       ? json_rpc[McpConstants::PARAMS_FIELD]
                                       : json::object());
        return;
      } else {
        // TODO(guoyilin42): Handle this more elegantly to avoid an unnecessary copy here. This can
        // be addressed later when the JSON parser is updated.
        mcp_operation_ = McpOperation::Unspecified;
        request_body_str_ = json_rpc.dump();
        status_ = BridgeStatus::RequestPassthrough;
        setParsingMetadata(method, json_rpc.contains(McpConstants::PARAMS_FIELD)
                                       ? json_rpc[McpConstants::PARAMS_FIELD]
                                       : json::object());
      }
    }
  } else if (method == McpConstants::Methods::INITIALIZE) {
    mcp_operation_ = McpOperation::Initialization;
    if (json_rpc.contains(McpConstants::PARAMS_FIELD) &&
        json_rpc[McpConstants::PARAMS_FIELD].contains(McpConstants::PROTOCOL_VERSION_FIELD) &&
        json_rpc[McpConstants::PARAMS_FIELD][McpConstants::PROTOCOL_VERSION_FIELD].is_string()) {
      setParsingMetadata(method, json_rpc.contains(McpConstants::PARAMS_FIELD)
                                     ? json_rpc[McpConstants::PARAMS_FIELD]
                                     : json::object());
      decoder_callbacks_->sendLocalReply(
          Http::Code::OK,
          generateInitializeResponse(
              *session_id_, server_name_,
              json_rpc[McpConstants::PARAMS_FIELD][McpConstants::PROTOCOL_VERSION_FIELD]
                  .get<std::string>())
              .dump(),
          [](Http::ResponseHeaderMap& headers) {
            headers.setContentType(Http::Headers::get().ContentTypeValues.Json);
          },
          Grpc::Status::WellKnownGrpcStatus::Ok, "mcp_json_rest_bridge_filter_initialize");
      return;
    }
    sendErrorResponse(
        Http::Code::BadRequest, BridgeStatus::RequestInitializeNotValid,
        generateErrorJsonResponse(-32602, "Missing valid protocolVersion in initialize "
                                          "request")
            .dump(),
        nullptr, method,
        json_rpc.contains(McpConstants::PARAMS_FIELD) ? json_rpc[McpConstants::PARAMS_FIELD]
                                                      : json::object());
  } else if (method == McpConstants::Methods::NOTIFICATION_INITIALIZED) {
    mcp_operation_ = McpOperation::InitializationAck;
    setParsingMetadata(method, json_rpc.contains(McpConstants::PARAMS_FIELD)
                                   ? json_rpc[McpConstants::PARAMS_FIELD]
                                   : json::object());
    // TODO(guoyilin42): We may need to explicitly set `content-length: 0` to prevent curl from
    // hanging. `modify_headers` fails here as `sendLocalReply` removes it for empty bodies.
    decoder_callbacks_->sendLocalReply(Http::Code::Accepted, "", nullptr,
                                       Grpc::Status::WellKnownGrpcStatus::Ok,
                                       "mcp_json_rest_bridge_filter_initialize_ack");
  } else if (method == McpConstants::Methods::TOOLS_CALL) {
    mcp_operation_ = McpOperation::ToolsCall;
    if (config_->traceContextExtraction() && request_headers.has_value()) {
      ENVOY_STREAM_LOG(debug, "Trace context extraction is enabled for tools/call method.",
                       *decoder_callbacks_);
      McpTraceContext trace_context(json_rpc);
      setTraceContextHeaders(*request_headers, trace_context);
    }
    mapMcpToolToApiBackend(json_rpc, per_route_config);
  } else {
    sendErrorResponse(
        Http::Code::BadRequest, BridgeStatus::RequestMethodNotSupported,
        generateErrorJsonResponse(-32601, absl::StrCat("Method ", method, " is not supported"))
            .dump(),
        nullptr, method,
        json_rpc.contains(McpConstants::PARAMS_FIELD) ? json_rpc[McpConstants::PARAMS_FIELD]
                                                      : json::object());
    return;
  }
}

void McpJsonRestBridgeFilter::encodeJsonRpcData(Http::ResponseHeaderMapOptRef response_headers) {
  const size_t total_size = response_body_.length();
  const char* json_ptr = static_cast<const char*>(response_body_.linearize(total_size));
  ENVOY_STREAM_LOG(debug, "Encoding Json-RPC data from response body: {}", *encoder_callbacks_,
                   absl::string_view(json_ptr, total_size));
  switch (mcp_operation_) {
  case McpOperation::ToolsList: {
    json tools = json::parse(json_ptr, json_ptr + total_size, /*parser_callback_t=*/nullptr,
                             /*allow_exceptions=*/false);
    if (tools.is_discarded() ||
        getResponseCode(response_headers) >= static_cast<int>(Http::Code::BadRequest)) {
      ENVOY_STREAM_LOG(error, "Tool list response is invalid or has error status code.",
                       *encoder_callbacks_);
      json ret = {
          {McpConstants::JSONRPC_FIELD, McpConstants::JSONRPC_VERSION},
          {McpConstants::ID_FIELD, *session_id_},
          {McpConstants::ERROR_FIELD, generateErrorJsonResponse(-32000, "Server error")},
      };
      response_body_str_ = ret.dump();
      setResponseMetadata(getResponseCode(response_headers) >=
                                  static_cast<int>(Http::Code::BadRequest)
                              ? BridgeStatus::ResponseBackendError
                              : BridgeStatus::ResponseParseError,
                          getResponseCode(response_headers));
      break;
    }
    json ret = {
        {McpConstants::JSONRPC_FIELD, McpConstants::JSONRPC_VERSION},
        {McpConstants::ID_FIELD, *session_id_},
        {McpConstants::RESULT_FIELD, tools},
    };
    response_body_str_ = ret.dump();
    setResponseMetadata(BridgeStatus::Ok, getResponseCode(response_headers));
    break;
  }
  case McpOperation::ToolsCall: {
    // The tool call response is in JSON REST format. Translates it to JSON-RPC.
    if (!utf8_range::IsStructurallyValid(absl::string_view(json_ptr, total_size))) {
      ENVOY_STREAM_LOG(
          warn,
          "API backend returns an invalid UTF-8 payload response. Returns error back to client.",
          *encoder_callbacks_);
      response_body_str_ =
          translateJsonRestResponseToJsonRpc("Backend response returns an invalid UTF-8 payload.",
                                             *session_id_, true)
              .dump();
      setResponseMetadata(BridgeStatus::ResponseInvalidUtf8, getResponseCode(response_headers));
    } else {
      bool is_error = getResponseCode(response_headers) >= static_cast<int>(Http::Code::BadRequest);
      response_body_str_ = translateJsonRestResponseToJsonRpc(
                               absl::string_view(json_ptr, total_size), *session_id_, is_error)
                               .dump();
      if (is_error) {
        setResponseMetadata(BridgeStatus::ResponseBackendError, getResponseCode(response_headers));
      } else {
        setResponseMetadata(BridgeStatus::Ok, getResponseCode(response_headers));
      }
    }
    break;
  }
  case McpOperation::OperationFailed: {
    // TODO(guoyilin42): Construct the full JSON-RPC error response directly in `sendErrorResponse`
    // to avoid this inefficient serialization-then-deserialization cycle and simplify the code.
    json error = json::parse(json_ptr, json_ptr + total_size, /*parser_callback_t=*/nullptr,
                             /*allow_exceptions=*/false);
    if (error.is_discarded()) {
      ENVOY_STREAM_LOG(error, "Failed to parse error response.", *encoder_callbacks_);
      return;
    }
    json ret = {{McpConstants::JSONRPC_FIELD, McpConstants::JSONRPC_VERSION},
                // If the ID is missing in the request, the ID in the response should be null.
                {McpConstants::ID_FIELD, session_id_.has_value() ? *session_id_ : json(nullptr)},
                {McpConstants::ERROR_FIELD, error}};
    response_body_str_ = ret.dump();
    break;
  }
  default:
    break;
  }

  if (response_headers.has_value()) {
    const auto transfer_encoding = response_headers->TransferEncoding();
    const bool is_chunked =
        transfer_encoding != nullptr &&
        absl::EqualsIgnoreCase(transfer_encoding->value().getStringView(),
                               Http::Headers::get().TransferEncodingValues.Chunked);
    if (is_chunked) {
      response_headers->removeContentLength();
    } else {
      response_headers->setContentLength(response_body_str_.size());
    }
    response_headers->setContentType(Http::Headers::get().ContentTypeValues.Json);
  }
}

void McpJsonRestBridgeFilter::mapMcpToolToApiBackend(
    const nlohmann::json& json_rpc, const McpJsonRestBridgePerRouteConfig* per_route_config) {
  const auto params_it = json_rpc.find(McpConstants::PARAMS_FIELD);
  if (params_it == json_rpc.end() || !params_it->is_object()) {
    ENVOY_STREAM_LOG(error,
                     "The tool call request is missing 'params' field or it's not an object.",
                     *decoder_callbacks_);
    sendErrorResponse(Http::Code::BadRequest, BridgeStatus::RequestToolParamsNotFound,
                      generateErrorJsonResponse(-32602, "Invalid params").dump(), nullptr,
                      McpConstants::Methods::TOOLS_CALL, json::object());
    return;
  }
  const auto& params = *params_it;

  const auto name_it = params.find(McpConstants::NAME_FIELD);
  if (name_it == params.end() || !name_it->is_string()) {
    ENVOY_STREAM_LOG(error, "Failed to get the name of the tool call request.",
                     *decoder_callbacks_);
    sendErrorResponse(Http::Code::BadRequest, BridgeStatus::RequestToolNameNotFound,
                      generateErrorJsonResponse(-32602, "Tool name not found").dump(), nullptr,
                      McpConstants::Methods::TOOLS_CALL, params);
    return;
  }
  const auto& tool_name = name_it->get<std::string>();

  absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule> http_rule =
      (per_route_config == nullptr) ? config_->getHttpRule(tool_name, server_name_, path_)
                                    : per_route_config->getHttpRule(tool_name, server_name_, path_);

  if (!http_rule.ok()) {
    ENVOY_STREAM_LOG(error, "Failed to get http rule for method: {}", *decoder_callbacks_,
                     tool_name);
    sendErrorResponse(Http::Code::BadRequest, BridgeStatus::RequestUnknownTool,
                      generateErrorJsonResponse(-32602, "Unknown tool").dump(), nullptr,
                      McpConstants::Methods::TOOLS_CALL, params);
    return;
  }

  // Set the per-request streaming flag based on the tool's config.
  text_content_streaming_enabled_ =
      (per_route_config == nullptr)
          ? config_->textContentStreamingEnabled(tool_name, server_name_, path_)
          : per_route_config->textContentStreamingEnabled(tool_name, server_name_, path_);

  const auto arguments_it = params.find(McpConstants::ARGUMENTS_FIELD);
  if (arguments_it != params.end() && !arguments_it->is_object()) {
    ENVOY_STREAM_LOG(error, "The arguments of the tool call request must be an object.",
                     *decoder_callbacks_);
    sendErrorResponse(Http::Code::BadRequest, BridgeStatus::RequestToolArgumentsInvalid,
                      generateErrorJsonResponse(-32602, "Tool arguments must be an object").dump(),
                      nullptr, McpConstants::Methods::TOOLS_CALL, params);
    return;
  }

  const nlohmann::json empty_arguments = nlohmann::json::object();
  const nlohmann::json& arguments = arguments_it != params.end() ? *arguments_it : empty_arguments;

  absl::StatusOr<HttpRequest> http_request = buildHttpRequest(*http_rule, arguments);
  if (!http_request.ok()) {
    ENVOY_STREAM_LOG(error, "Failed to build HTTP request for method: {} with status: {}",
                     *decoder_callbacks_, tool_name, http_request.status());
    sendErrorResponse(Http::Code::BadRequest, BridgeStatus::RequestToolTranscodingFailure,
                      generateErrorJsonResponse(-32602, "Failed to build HTTP request").dump(),
                      nullptr, McpConstants::Methods::TOOLS_CALL, params);
    return;
  }

  request_body_str_ = http_request->body.is_null() ? "" : http_request->body.dump();
  ENVOY_STREAM_LOG(debug, "Mapping MCP tool to HTTP request url: {} method: {} body: {}",
                   *decoder_callbacks_, http_request->url, http_request->method, request_body_str_);

  auto request_headers = decoder_callbacks_->requestHeaders();
  if (request_headers.has_value()) {
    request_headers->setPath(http_request->url);
    request_headers->setMethod(http_request->method);
    const auto transfer_encoding = request_headers->TransferEncoding();
    const bool is_chunked =
        transfer_encoding != nullptr &&
        absl::EqualsIgnoreCase(transfer_encoding->value().getStringView(),
                               Http::Headers::get().TransferEncodingValues.Chunked);
    if (is_chunked) {
      request_headers->removeContentLength();
    } else {
      request_headers->setContentLength(request_body_str_.size());
    }
    request_headers->setContentType(Http::Headers::get().ContentTypeValues.Json);
    // Set AcceptEncoding to "identity" to prevent server encoding the response.
    request_headers->setCopy(Http::CustomHeaders::get().AcceptEncoding,
                             Http::CustomHeaders::get().AcceptEncodingValues.Identity);
  }

  if (config_->clearRouteCache() && decoder_callbacks_->downstreamCallbacks().has_value()) {
    decoder_callbacks_->downstreamCallbacks()->clearRouteCache();
  }
  setParsingMetadata(McpConstants::Methods::TOOLS_CALL, params);
}

void McpJsonRestBridgeFilter::sendErrorResponse(
    Http::Code response_code, BridgeStatus status, absl::string_view response_body,
    std::function<void(Http::ResponseHeaderMap&)> modify_headers, absl::string_view method,
    const nlohmann::json& params, Grpc::Status::GrpcStatus grpc_status) {
  ENVOY_STREAM_LOG(error, "Sending error response with status: {}", *decoder_callbacks_,
                   bridgeStatusToString(status));
  status_ = status;
  mcp_operation_ = McpOperation::OperationFailed;
  setParsingMetadata(method, params);

  decoder_callbacks_->sendLocalReply(response_code, response_body, modify_headers, grpc_status,
                                     bridgeStatusToString(status));
}

void McpJsonRestBridgeFilter::setDynamicMetadata() {
  Protobuf::Struct dynamic_metadata;
  (*dynamic_metadata.mutable_fields())[BridgeStatusValues::STATUS].set_string_value(
      std::string(bridgeStatusToString(status_)));

  if (!mcp_method_.empty()) {
    (*dynamic_metadata.mutable_fields())[McpConstants::METHOD_FIELD].set_string_value(mcp_method_);
  }
  if (has_params_) {
    *(*dynamic_metadata.mutable_fields())[McpConstants::PARAMS_FIELD].mutable_struct_value() =
        mcp_params_;
  }
  if (backend_response_code_.has_value()) {
    (*dynamic_metadata.mutable_fields())[McpConstants::BACKEND_RESPONSE_CODE_FIELD]
        .set_number_value(*backend_response_code_);
  }

  decoder_callbacks_->streamInfo().setDynamicMetadata(
      std::string(decoder_callbacks_->filterConfigName()), dynamic_metadata);
  ENVOY_STREAM_LOG(debug, "MCP JSON REST Bridge filter set dynamic metadata: {}",
                   *decoder_callbacks_, dynamic_metadata.DebugString());
}

void McpJsonRestBridgeFilter::setParsingMetadata(absl::string_view method,
                                                 const nlohmann::json& params) {
  if (!config_->shouldStoreToDynamicMetadata()) {
    return;
  }

  mcp_method_ = std::string(method);
  has_params_ = false;
  mcp_params_.Clear();
  if (params.is_object() && !params.empty()) {
    absl::Status status = MessageUtil::loadFromJsonNoThrow(params.dump(), mcp_params_);
    if (status.ok()) {
      has_params_ = !mcp_params_.fields().empty();
    } else {
      ENVOY_STREAM_LOG(warn, "Failed to parse params as Protobuf Struct: {}", *decoder_callbacks_,
                       status);
    }
  }

  setDynamicMetadata();
}

void McpJsonRestBridgeFilter::setResponseMetadata(BridgeStatus status,
                                                  std::optional<uint64_t> response_code) {
  status_ = status;
  backend_response_code_ = response_code;
  if (!config_->shouldStoreToDynamicMetadata()) {
    return;
  }

  setDynamicMetadata();
}

absl::Status McpJsonRestBridgeFilter::validateJsonRpcIdAndMethod(const nlohmann::json& json_rpc) {
  absl::StatusOr<nlohmann::json> session_id = getSessionId(json_rpc);
  if (session_id.ok()) {
    session_id_ = *session_id;
  }
  if (!json_rpc.contains(McpConstants::METHOD_FIELD)) {
    sendErrorResponse(Http::Code::BadRequest, BridgeStatus::RequestMethodNotFound,
                      generateErrorJsonResponse(-32601, "Missing method field").dump());
    return absl::InvalidArgumentError("Missing method field");
  } else if (!json_rpc[McpConstants::METHOD_FIELD].is_string()) {
    sendErrorResponse(Http::Code::BadRequest, BridgeStatus::RequestMethodNotString,
                      generateErrorJsonResponse(-32601, "Method field is not a string").dump());
    return absl::InvalidArgumentError("Method field is not a string");
  } else if (json_rpc[McpConstants::METHOD_FIELD] ==
             McpConstants::Methods::NOTIFICATION_INITIALIZED) {
    // The notifications/initialized request is not required to have an ID
    // field.
  } else if (!session_id.ok()) {
    sendErrorResponse(Http::Code::BadRequest, BridgeStatus::RequestIdNotFound,
                      generateErrorJsonResponse(-32600, "Missing ID field").dump(), nullptr,
                      json_rpc[McpConstants::METHOD_FIELD].get<std::string>(),
                      json_rpc.contains(McpConstants::PARAMS_FIELD)
                          ? json_rpc[McpConstants::PARAMS_FIELD]
                          : json::object());
    return absl::InvalidArgumentError("Missing ID field");
  }
  return absl::OkStatus();
}

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
