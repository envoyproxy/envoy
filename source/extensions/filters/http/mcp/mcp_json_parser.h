#pragma once

#include <memory>
#include <stack>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

/**
 * MCP protocol constants
 */
namespace McpConstants {
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
constexpr absl::string_view INITIALIZED = "initialized";
constexpr absl::string_view SHUTDOWN = "shutdown";

// Sampling
constexpr absl::string_view SAMPLING_CREATE_MESSAGE = "sampling/createMessage";

// Utility
constexpr absl::string_view PING = "ping";

// Notification prefix
constexpr absl::string_view NOTIFICATION_PREFIX = "notifications/";

// Specific notifications
constexpr absl::string_view NOTIFICATION_RESOURCES_LIST_CHANGED =
    "notifications/resources/list_changed";
constexpr absl::string_view NOTIFICATION_RESOURCES_UPDATED = "notifications/resources/updated";
constexpr absl::string_view NOTIFICATION_TOOLS_LIST_CHANGED = "notifications/tools/list_changed";
constexpr absl::string_view NOTIFICATION_PROMPTS_LIST_CHANGED =
    "notifications/prompts/list_changed";
constexpr absl::string_view NOTIFICATION_PROGRESS = "notifications/progress";
constexpr absl::string_view NOTIFICATION_MESSAGE = "notifications/message";
constexpr absl::string_view NOTIFICATION_CANCELLED = "notifications/cancelled";
constexpr absl::string_view NOTIFICATION_INITIALIZED = "notifications/initialized";
} // namespace Methods
} // namespace McpConstants

/**
 * Configuration for MCP field extraction
 */
class McpParserConfig {
public:
  struct AttributeExtractionRule {
    std::string path; // JSON path (e.g., "params.name")

    AttributeExtractionRule(const std::string& p) : path(p) {}
  };

  // Create from proto configuration
  static McpParserConfig
  fromProto(const envoy::extensions::filters::http::mcp::v3::ParserConfig& proto);

  // Get extraction policy for a specific method
  const std::vector<AttributeExtractionRule>& getFieldsForMethod(const std::string& method) const;

  // Add method configuration
  void addMethodConfig(absl::string_view method, std::vector<AttributeExtractionRule> fields);

  // Get all global fields to always extract
  const absl::flat_hash_set<std::string>& getAlwaysExtract() const { return always_extract_; }

  // Create default config (minimal extraction)
  static McpParserConfig createDefault();

private:
  void initializeDefaults();

  // Per-method field policies
  absl::flat_hash_map<std::string, std::vector<AttributeExtractionRule>> method_fields_;

  // Global fields to always extract
  absl::flat_hash_set<std::string> always_extract_;
};

/**
 * MCP JSON field extractor with early stopping optimization
 */
class McpFieldExtractor : public ProtobufUtil::converter::ObjectWriter,
                          public Logger::Loggable<Logger::Id::mcp> {
public:
  McpFieldExtractor(Protobuf::Struct& metadata, const McpParserConfig& config);

  // ObjectWriter implementation
  McpFieldExtractor* StartObject(absl::string_view name) override;
  McpFieldExtractor* EndObject() override;
  McpFieldExtractor* StartList(absl::string_view name) override;
  McpFieldExtractor* EndList() override;
  McpFieldExtractor* RenderString(absl::string_view name, absl::string_view value) override;
  McpFieldExtractor* RenderInt32(absl::string_view name, int32_t value) override;
  McpFieldExtractor* RenderUint32(absl::string_view name, uint32_t value) override;
  McpFieldExtractor* RenderInt64(absl::string_view name, int64_t value) override;
  McpFieldExtractor* RenderUint64(absl::string_view name, uint64_t value) override;
  McpFieldExtractor* RenderDouble(absl::string_view name, double value) override;
  McpFieldExtractor* RenderFloat(absl::string_view name, float value) override;
  McpFieldExtractor* RenderBool(absl::string_view name, bool value) override;
  McpFieldExtractor* RenderNull(absl::string_view name) override;
  McpFieldExtractor* RenderBytes(absl::string_view name, absl::string_view value) override;

  // Check if we can stop parsing early
  bool shouldStopParsing() const { return can_stop_parsing_; }

  // Finalize extraction after parsing complete
  void finalizeExtraction();

  // MCP validation getters
  bool isValidMcp() const { return is_valid_mcp_; }
  const std::string& getMethod() const { return method_; }

private:
  // Check if we have all fields we need for early stop
  void checkEarlyStop();

  // Store field in temp storage
  void storeField(const std::string& path, const Protobuf::Value& value);

  // Copy selected fields from temp to final
  void copySelectedFields();
  void copyFieldByPath(const std::string& path);

  // Validate required fields
  void validateRequiredFields();

  // Helper to build full path from cache
  std::string buildFullPath(absl::string_view name) const;

  Protobuf::Struct temp_storage_;   // Store all fields temporarily
  Protobuf::Struct& root_metadata_; // Final filtered metadata
  const McpParserConfig& config_;

  // Stack for building temp storage
  struct NestedContext {
    Protobuf::Struct* struct_ptr;
    std::string field_name;
  };
  std::stack<NestedContext> context_stack_;

  // Current path tracking
  std::vector<std::string> path_stack_;

  // Track collected fields
  absl::flat_hash_set<std::string> collected_fields_;
  absl::flat_hash_set<std::string> extracted_fields_;

  // MCP state
  std::string method_;
  bool is_valid_mcp_{false};
  bool has_jsonrpc_{false};
  bool has_method_{false};

  // Early stop optimization
  bool can_stop_parsing_{false};

  // Validation
  std::vector<std::string> missing_required_fields_;

  int depth_{0};
  int array_depth_{0};

  // Performance optimization caches
  std::string current_path_cache_;
  size_t fields_needed_{0};
  size_t fields_collected_count_{0};
  bool fields_needed_updated_{false};
  bool is_notification_{false};
};

/**
 * MCP JSON parser with selective field extraction
 */
class McpJsonParser : public Logger::Loggable<Logger::Id::mcp> {
public:
  // Constructor with optional config (defaults to minimal extraction)
  explicit McpJsonParser(const McpParserConfig& config = McpParserConfig::createDefault());

  // Parse a chunk of JSON data
  absl::Status parse(absl::string_view data);

  // Finish parsing
  absl::Status finishParse();

  // Check if this is a valid MCP request
  bool isValidMcpRequest() const;

  bool isAllFieldsCollected() const { return all_fields_collected_; }

  // Get the method string
  const std::string& getMethod() const;

  // Get the extracted metadata (only contains configured fields)
  const Protobuf::Struct& metadata() const { return metadata_; }

  // Helper to get nested value from metadata
  const Protobuf::Value* getNestedValue(const std::string& dotted_path) const;

  // Reset parser for reuse
  void reset();

private:
  McpParserConfig config_;
  Protobuf::Struct metadata_;
  std::unique_ptr<McpFieldExtractor> extractor_;
  std::unique_ptr<ProtobufUtil::converter::JsonStreamParser> stream_parser_;
  bool parsing_started_{false};
  bool all_fields_collected_{false};
};

// Compatibility aliases
using JsonPathParser = McpJsonParser;
using FieldExtractorObjectWriter = McpFieldExtractor;
using ParserConfig = McpParserConfig;

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
