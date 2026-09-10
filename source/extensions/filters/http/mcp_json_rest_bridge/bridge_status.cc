#include "source/extensions/filters/http/mcp_json_rest_bridge/bridge_status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

absl::string_view bridgeStatusToString(BridgeStatus status) {
  switch (status) {
  case BridgeStatus::Ok:
    return BridgeStatusValues::OK;
  case BridgeStatus::HttpRequestMethodNotPost:
    return BridgeStatusValues::HTTP_REQUEST_METHOD_NOT_POST;
  case BridgeStatus::RequestTooLarge:
    return BridgeStatusValues::REQUEST_TOO_LARGE;
  case BridgeStatus::RequestFailedToParseJsonRpc:
    return BridgeStatusValues::REQUEST_FAILED_TO_PARSE_JSON_RPC;
  case BridgeStatus::RequestUnsupportedMcpVersion:
    return BridgeStatusValues::REQUEST_UNSUPPORTED_MCP_VERSION;
  case BridgeStatus::RequestInitializeNotValid:
    return BridgeStatusValues::REQUEST_INITIALIZE_NOT_VALID;
  case BridgeStatus::RequestMcpMethodNotSupported:
    return BridgeStatusValues::REQUEST_MCP_METHOD_NOT_SUPPORTED;
  case BridgeStatus::RequestMcpMethodMalformed:
    return BridgeStatusValues::REQUEST_MCP_METHOD_MALFORMED;
  case BridgeStatus::RequestIdNotFound:
    return BridgeStatusValues::REQUEST_ID_NOT_FOUND;
  case BridgeStatus::RequestToolsCallToolNameMissing:
    return BridgeStatusValues::REQUEST_TOOLS_CALL_TOOL_NAME_MISSING;
  case BridgeStatus::RequestToolsCallToolNameUnknown:
    return BridgeStatusValues::REQUEST_TOOLS_CALL_TOOL_NAME_UNKNOWN;
  case BridgeStatus::RequestToolsCallArgumentsMalformed:
    return BridgeStatusValues::REQUEST_TOOLS_CALL_ARGUMENTS_MALFORMED;
  case BridgeStatus::RequestToolsCallMissingRequiredArg:
    return BridgeStatusValues::REQUEST_TOOLS_CALL_MISSING_REQUIRED_ARG;
  case BridgeStatus::RequestToolsCallPathTraversalRejected:
    return BridgeStatusValues::REQUEST_TOOLS_CALL_PATH_TRAVERSAL_REJECTED;
  case BridgeStatus::InternalToolsCallInvalidHttpRule:
    return BridgeStatusValues::INTERNAL_TOOLS_CALL_INVALID_HTTP_RULE;
  case BridgeStatus::InternalToolsListPassthrough:
    return BridgeStatusValues::INTERNAL_TOOLS_LIST_PASSTHROUGH;
  case BridgeStatus::ResponseTooLarge:
    return BridgeStatusValues::RESPONSE_TOO_LARGE;
  case BridgeStatus::ResponseToolsCallInvalidUtf8:
    return BridgeStatusValues::RESPONSE_TOOLS_CALL_INVALID_UTF8;
  case BridgeStatus::ResponseHttpStatusError:
    return BridgeStatusValues::RESPONSE_HTTP_STATUS_ERROR;
  case BridgeStatus::ResponseFailedToParseJsonRpc:
    return BridgeStatusValues::RESPONSE_FAILED_TO_PARSE_JSON_RPC;
  }
  return BridgeStatusValues::OK;
}

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
