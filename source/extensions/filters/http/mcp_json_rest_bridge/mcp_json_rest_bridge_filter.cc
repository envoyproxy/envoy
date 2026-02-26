#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

#include "source/extensions/filters/common/mcp/constants.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::nlohmann::json;
namespace McpConstants = Envoy::Extensions::Filters::Common::Mcp::McpConstants;

absl::StatusOr<int> getSessionId(const json& json_rpc) {
  if (auto it = json_rpc.find(McpConstants::ID_FIELD); it != json_rpc.end()) {
    if (it->is_number_integer()) {
      return it->get<int>();
    }
    if (it->is_string()) {
      int int_id;
      // TODO(guoyilin42): Support non-numeric string IDs as MCP is JSON-RPC compliant.
      if (absl::SimpleAtoi(it->get<std::string>(), &int_id)) {
        return int_id;
      }
    }
    return absl::InvalidArgumentError("JSON-RPC request ID is not an integer or a numeric string.");
  }
  return absl::InvalidArgumentError("JSON-RPC request (except notification) does not have an ID.");
}

json generateInitializeResponse(int session_id, absl::string_view server_name) {
  json ret;
  ret[McpConstants::JSONRPC_FIELD] = McpConstants::JSONRPC_VERSION;
  ret[McpConstants::ID_FIELD] = session_id;

  json result;
  result[McpConstants::PROTOCOL_VERSION_FIELD] = McpConstants::LATEST_SUPPORTED_MCP_VERSION;
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
  json error = json::object();
  error["code"] = error_code;
  error["message"] = error_message;
  return error;
}

} // namespace

McpJsonRestBridgeFilterConfig::McpJsonRestBridgeFilterConfig(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge&
        proto_config)
    : proto_config_(proto_config) {
  for (const auto& tool : proto_config.tool_config().tools()) {
    tool_to_http_rule_[tool.name()] = tool.http_rule();
  }
  ENVOY_LOG(debug, "Received MCP JSON REST Bridge config: {}", proto_config_.DebugString());
}

// TODO(guoyilin42): Use it for future tools/call implementation.
absl::StatusOr<envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule>
McpJsonRestBridgeFilterConfig::getHttpRule(absl::string_view tool_name) const {
  auto it = tool_to_http_rule_.find(tool_name);
  if (it == tool_to_http_rule_.end()) {
    return absl::InvalidArgumentError(
        fmt::format("Failed to find http rule for tool_name: {}", tool_name));
  }
  return it->second;
}

Http::FilterHeadersStatus
McpJsonRestBridgeFilter::decodeHeaders(Http::RequestHeaderMap& request_headers, bool) {
  if (request_headers.getPathValue() != "/mcp") {
    // Only intercept /mcp requests and pass through other requests.
    return Http::FilterHeadersStatus::Continue;
  }

  mcp_operation_ = McpOperation::Undecided;
  // TODO(guoyilin42): Strip port number from server_name_.
  server_name_ = std::string(request_headers.getHostValue());

  if (request_headers.getMethodValue() != "POST") {
    ENVOY_LOG(warn, "Only POST method is supported for MCP. Received: {}",
              request_headers.getMethodValue());
    // TODO(guoyilin42): Consider adding an Allow header when doing error handling.
    decoder_callbacks_->sendLocalReply(Http::Code::MethodNotAllowed, "Method Not Allowed", nullptr,
                                       Grpc::Status::WellKnownGrpcStatus::InvalidArgument,
                                       "mcp_json_rest_bridge_filter_not_post");
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus McpJsonRestBridgeFilter::decodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (mcp_operation_ == McpOperation::Unspecified) {
    return Http::FilterDataStatus::Continue;
  }

  // TODO(guoyilin42): Add hard limit for the buffer size and flow control if possible.
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
    ENVOY_LOG(error, "Failed to parse JSON-RPC request body.");
    sendErrorResponse(Http::Code::BadRequest,
                      "mcp_json_rest_bridge_filter_failed_to_parse_json_rpc_request",
                      generateErrorJsonResponse(-32700, "JSON parse error").dump());
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  handleMcpMethod(request_body_json);

  // TODO(guoyilin42): Clear the route cache if needed.

  if (mcp_operation_ == McpOperation::Initialization ||
      mcp_operation_ == McpOperation::InitializationAck ||
      mcp_operation_ == McpOperation::OperationFailed) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus McpJsonRestBridgeFilter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
  // TODO(guoyilin42): handle response headers.
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus McpJsonRestBridgeFilter::encodeData(Buffer::Instance&, bool) {
  // TODO(guoyilin42): handle response data.
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus McpJsonRestBridgeFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  // TODO(guoyilin42): handle response trailers.
  return Http::FilterTrailersStatus::Continue;
}

void McpJsonRestBridgeFilter::handleMcpMethod(const nlohmann::json& json_rpc) {
  ENVOY_LOG(info, "Handling MCP JSON-RPC: {}", json_rpc.dump());
  if (!validateJsonRpcIdAndMethod(json_rpc).ok()) {
    return;
  }

  std::string method = json_rpc[McpConstants::METHOD_FIELD];
  if (method == McpConstants::Methods::TOOLS_LIST) {
    mcp_operation_ = McpOperation::ToolsList;
    // TODO(guoyilin42): handle tools/list request.
  } else if (method == McpConstants::Methods::INITIALIZE) {
    mcp_operation_ = McpOperation::Initialization;
    if (json_rpc.contains("params") &&
        json_rpc["params"].contains(McpConstants::PROTOCOL_VERSION_FIELD) &&
        json_rpc["params"][McpConstants::PROTOCOL_VERSION_FIELD].is_string()) {
      decoder_callbacks_->sendLocalReply(
          Http::Code::OK, generateInitializeResponse(*session_id_, server_name_).dump(), nullptr,
          Grpc::Status::WellKnownGrpcStatus::Ok, "mcp_json_rest_bridge_filter_initialize");
      return;
    }
    sendErrorResponse(
        Http::Code::BadRequest, "mcp_json_rest_bridge_filter_initialize_request_not_valid",
        generateErrorJsonResponse(-32602, "Missing valid protocolVersion in initialize "
                                          "request")
            .dump());
  } else if (method == McpConstants::Methods::NOTIFICATION_INITIALIZED) {
    mcp_operation_ = McpOperation::InitializationAck;
    decoder_callbacks_->sendLocalReply(
        Http::Code::Accepted, "",
        [](Http::ResponseHeaderMap& headers) {
          headers.addCopy(Http::LowerCaseString("content-length"), "0");
        },
        Grpc::Status::WellKnownGrpcStatus::Ok, "mcp_json_rest_bridge_filter_initialize_ack");
  } else if (method == McpConstants::Methods::TOOLS_CALL) {
    mcp_operation_ = McpOperation::ToolsCall;
    // TODO(guoyilin42): handle tools/call request.
  } else {
    sendErrorResponse(
        Http::Code::BadRequest, "mcp_json_rest_bridge_filter_method_not_supported",
        generateErrorJsonResponse(-32601, absl::StrCat("Method ", method, " is not supported"))
            .dump());
    return;
  }
}

void McpJsonRestBridgeFilter::sendErrorResponse(Http::Code response_code,
                                                absl::string_view response_code_details,
                                                absl::string_view response_body) {
  ENVOY_LOG(error, "Sending error response with response code details: {}", response_code_details);
  mcp_operation_ = McpOperation::OperationFailed;
  decoder_callbacks_->sendLocalReply(response_code, response_body, nullptr,
                                     Grpc::Status::WellKnownGrpcStatus::Internal,
                                     response_code_details);
}

absl::Status McpJsonRestBridgeFilter::validateJsonRpcIdAndMethod(const nlohmann::json& json_rpc) {
  absl::StatusOr<int> session_id = getSessionId(json_rpc);
  if (session_id.ok()) {
    session_id_ = *session_id;
  }
  if (!json_rpc.contains(McpConstants::METHOD_FIELD)) {
    sendErrorResponse(Http::Code::BadRequest, "mcp_json_rest_bridge_filter_method_not_found",
                      generateErrorJsonResponse(-32601, "Missing method field").dump());
    return absl::InvalidArgumentError("Missing method field");
  } else if (!json_rpc[McpConstants::METHOD_FIELD].is_string()) {
    sendErrorResponse(Http::Code::BadRequest, "mcp_json_rest_bridge_filter_method_not_string",
                      generateErrorJsonResponse(-32601, "Method field is not a string").dump());
    return absl::InvalidArgumentError("Method field is not a string");
  } else if (json_rpc[McpConstants::METHOD_FIELD] ==
             McpConstants::Methods::NOTIFICATION_INITIALIZED) {
    // The notifications/initialized request is not required to have an ID
    // field.
  } else if (!session_id.ok()) {
    sendErrorResponse(Http::Code::BadRequest, "mcp_json_rest_bridge_filter_id_not_found",
                      generateErrorJsonResponse(-32600, "Missing ID field").dump());
    return absl::InvalidArgumentError("Missing ID field");
  }
  return absl::OkStatus();
}

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
