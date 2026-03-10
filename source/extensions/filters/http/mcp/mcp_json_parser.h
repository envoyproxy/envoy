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
#include "source/extensions/filters/common/mcp/constants.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

using namespace Filters::Common::Mcp::McpConstants;

/**
 * Configuration for MCP field extraction
 */
class McpParserConfig {
public:
  struct FieldRequirements {
    std::vector<std::string> required;
    std::vector<std::string> optional;
  };

  // Rule for extracting a specific attribute from the JSON payload.
  struct AttributeExtractionRule {
    // JSON path to extract (e.g., "params.name", "params.uri").
    std::string path;

    AttributeExtractionRule(const std::string& p) : path(p) {}
  };

  // Method config entry for user-configured rules
  struct MethodConfigEntry {
    std::string method_pattern; // Method pattern (exact or with trailing "*" for prefix)
    std::string group;          // Group name, empty means use built-in
    std::vector<AttributeExtractionRule> extraction_rules;
  };

  // Create from proto configuration
  static McpParserConfig
  fromProto(const envoy::extensions::filters::http::mcp::v3::ParserConfig& proto);

  // Get extraction policy for a specific method
  const std::vector<AttributeExtractionRule>& getFieldsForMethod(const std::string& method) const;

  // Get merged requirements for a specific method (global + method-specific).
  const FieldRequirements& getFieldRequirementsForMethod(const std::string& method) const;

  // Add method configuration
  void addMethodConfig(absl::string_view method, std::vector<AttributeExtractionRule> fields);

  // Get all global fields to always extract
  const absl::flat_hash_set<std::string>& getAlwaysExtract() const { return always_extract_; }

  // Get the group metadata key (empty if disabled)
  const std::string& groupMetadataKey() const { return group_metadata_key_; }

  // Get the method group for a given method name
  // Returns the group name based on user config first, then built-in groups
  std::string getMethodGroup(const std::string& method) const;

  // Create default config (minimal extraction)
  static McpParserConfig createDefault();

private:
  void initializeDefaults();
  std::string getBuiltInMethodGroup(const std::string& method) const;
  void buildFieldRequirements();
  void buildMethodRequirements(const std::vector<AttributeExtractionRule>& method_fields,
                               FieldRequirements& requirements) const;

  // Per-method field policies
  absl::flat_hash_map<std::string, std::vector<AttributeExtractionRule>> method_fields_;

  // User-configured method configs
  std::vector<MethodConfigEntry> method_configs_;

  // Method group configuration
  std::string group_metadata_key_;

  // Global fields to always extract
  absl::flat_hash_set<std::string> always_extract_;

  FieldRequirements default_requirements_;
  absl::flat_hash_map<std::string, FieldRequirements> method_requirements_;
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

  // Check if optional fields are configured for the current method
  bool hasOptionalFields();

  // Check if all required fields have been collected
  bool hasAllRequiredFields();

  // MCP validation getters
  bool isValidMcp() const { return is_valid_mcp_; }
  const std::string& getMethod() const { return method_; }

private:
  // Check if we have all fields we need for early stop
  void checkEarlyStop();

  // Update required/optional field lists once method is known
  void updateFieldRequirements();

  // Verify required fields are present
  bool requiredFieldsCollected() const;

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
  size_t required_fields_needed_{0};
  size_t fields_collected_count_{0};
  bool fields_needed_updated_{false};
  bool is_notification_{false};
  bool has_optional_fields_{false};
  int params_depth_{0}; // Depth when we entered "params" object (0 = not in params)

  std::vector<std::string> required_fields_;
  std::vector<std::string> optional_fields_;
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

  // Check if optional fields are configured for the current method
  bool hasOptionalFields();

  // Check if all required fields have been collected
  bool hasAllRequiredFields();

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
