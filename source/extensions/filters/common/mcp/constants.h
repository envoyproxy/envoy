#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Mcp {

/**
 * MCP protocol constants
 */
namespace McpConstants {
// Metadata namespace constants
constexpr absl::string_view McpfilterNamespace = "envoy.filters.http.mcp";
constexpr absl::string_view LegacyMetadataNamespace = "mcp_proxy";

// JSON-RPC constants
constexpr absl::string_view JSONRPC_VERSION = "2.0";
constexpr absl::string_view JSONRPC_FIELD = "jsonrpc";
constexpr absl::string_view METHOD_FIELD = "method";
constexpr absl::string_view ID_FIELD = "id";

// Method names
namespace Methods {
// Tools
constexpr absl::string_view TOOLS_CALL = "tools/call";
constexpr absl::string_view TOOLS_LIST = "tools/list";

// Resources
constexpr absl::string_view RESOURCES_READ = "resources/read";
constexpr absl::string_view RESOURCES_LIST = "resources/list";
constexpr absl::string_view RESOURCES_SUBSCRIBE = "resources/subscribe";
constexpr absl::string_view RESOURCES_UNSUBSCRIBE = "resources/unsubscribe";
constexpr absl::string_view RESOURCES_TEMPLATES_LIST = "resources/templates/list";

// Prompts
constexpr absl::string_view PROMPTS_GET = "prompts/get";
constexpr absl::string_view PROMPTS_LIST = "prompts/list";

// Completion
constexpr absl::string_view COMPLETION_COMPLETE = "completion/complete";

// Logging
constexpr absl::string_view LOGGING_SET_LEVEL = "logging/setLevel";

// Lifecycle
constexpr absl::string_view INITIALIZE = "initialize";

// Sampling
constexpr absl::string_view SAMPLING_CREATE_MESSAGE = "sampling/createMessage";

// Utility
constexpr absl::string_view PING = "ping";

// Notification prefix
constexpr absl::string_view NOTIFICATION_PREFIX = "notifications/";

// Specific notifications.
constexpr absl::string_view NOTIFICATION_INITIALIZED = "notifications/initialized";
constexpr absl::string_view NOTIFICATION_CANCELLED = "notifications/cancelled";
constexpr absl::string_view NOTIFICATION_PROGRESS = "notifications/progress";
constexpr absl::string_view NOTIFICATION_MESSAGE = "notifications/message";
constexpr absl::string_view NOTIFICATION_ROOTS_LIST_CHANGED = "notifications/roots/list_changed";
constexpr absl::string_view NOTIFICATION_RESOURCES_LIST_CHANGED =
    "notifications/resources/list_changed";
constexpr absl::string_view NOTIFICATION_RESOURCES_UPDATED = "notifications/resources/updated";
constexpr absl::string_view NOTIFICATION_TOOLS_LIST_CHANGED = "notifications/tools/list_changed";
constexpr absl::string_view NOTIFICATION_PROMPTS_LIST_CHANGED =
    "notifications/prompts/list_changed";
} // namespace Methods

// Method group names for classification
namespace MethodGroups {
constexpr absl::string_view LIFECYCLE = "lifecycle";
constexpr absl::string_view TOOL = "tool";
constexpr absl::string_view RESOURCE = "resource";
constexpr absl::string_view PROMPT = "prompt";
constexpr absl::string_view NOTIFICATION = "notification";
constexpr absl::string_view LOGGING = "logging";
constexpr absl::string_view SAMPLING = "sampling";
constexpr absl::string_view COMPLETION = "completion";
constexpr absl::string_view UNKNOWN = "unknown";
} // namespace MethodGroups

// JSON paths for attribute extraction
namespace Paths {
constexpr absl::string_view PARAMS_NAME = "params.name";
constexpr absl::string_view PARAMS_URI = "params.uri";
constexpr absl::string_view PARAMS_LEVEL = "params.level";
constexpr absl::string_view PARAMS_REF = "params.ref";
constexpr absl::string_view PARAMS_PROTOCOL_VERSION = "params.protocolVersion";
constexpr absl::string_view PARAMS_CLIENT_INFO_NAME = "params.clientInfo.name";
constexpr absl::string_view PARAMS_REQUEST_ID = "params.requestId";
constexpr absl::string_view PARAMS_PROGRESS_TOKEN = "params.progressToken";
constexpr absl::string_view PARAMS_PROGRESS = "params.progress";
constexpr absl::string_view PARAMS_META = "params._meta";
} // namespace Paths
} // namespace McpConstants

} // namespace Mcp
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
