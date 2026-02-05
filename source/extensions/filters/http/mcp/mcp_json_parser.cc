#include "source/extensions/filters/http/mcp/mcp_json_parser.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

using namespace McpConstants;

void McpParserConfig::addMethodConfig(absl::string_view method,
                                      std::vector<AttributeExtractionRule> fields) {
  const std::string method_key(method);
  method_fields_[method_key] = std::move(fields);
  buildFieldRequirements();
}

void McpParserConfig::initializeDefaults() {
  // Always extract core JSON-RPC fields
  always_extract_.insert("jsonrpc");
  always_extract_.insert("method");
  always_extract_.insert("id");

  // Tools
  addMethodConfig(Methods::TOOLS_CALL, {AttributeExtractionRule("params.name")});

  // Resources.
  addMethodConfig(Methods::RESOURCES_LIST, {});
  addMethodConfig(Methods::RESOURCES_READ, {AttributeExtractionRule("params.uri")});
  addMethodConfig(Methods::RESOURCES_SUBSCRIBE, {AttributeExtractionRule("params.uri")});
  addMethodConfig(Methods::RESOURCES_UNSUBSCRIBE, {AttributeExtractionRule("params.uri")});

  // Prompts.
  addMethodConfig(Methods::PROMPTS_LIST, {});
  addMethodConfig(Methods::PROMPTS_GET, {AttributeExtractionRule("params.name")});

  // Completion.
  addMethodConfig(Methods::COMPLETION_COMPLETE, {AttributeExtractionRule("params.ref")});

  // Logging
  addMethodConfig(Methods::LOGGING_SET_LEVEL, {AttributeExtractionRule("params.level")});

  // Lifecycle
  addMethodConfig(Methods::INITIALIZE, {AttributeExtractionRule("params.protocolVersion"),
                                        AttributeExtractionRule("params.clientInfo.name")});

  // Notifications.
  addMethodConfig(Methods::NOTIFICATION_INITIALIZED, {});
  addMethodConfig(Methods::NOTIFICATION_CANCELLED, {AttributeExtractionRule("params.requestId")});
  addMethodConfig(Methods::NOTIFICATION_PROGRESS, {AttributeExtractionRule("params.progressToken"),
                                                   AttributeExtractionRule("params.progress")});
  addMethodConfig(Methods::NOTIFICATION_MESSAGE, {AttributeExtractionRule("params.level")});
  addMethodConfig(Methods::NOTIFICATION_ROOTS_LIST_CHANGED, {});
  addMethodConfig(Methods::NOTIFICATION_RESOURCES_LIST_CHANGED, {});
  addMethodConfig(Methods::NOTIFICATION_RESOURCES_UPDATED, {AttributeExtractionRule("params.uri")});
  addMethodConfig(Methods::NOTIFICATION_TOOLS_LIST_CHANGED, {});
  addMethodConfig(Methods::NOTIFICATION_PROMPTS_LIST_CHANGED, {});
}

McpParserConfig
McpParserConfig::fromProto(const envoy::extensions::filters::http::mcp::v3::ParserConfig& proto) {
  McpParserConfig config;

  config.always_extract_.insert("jsonrpc");
  config.always_extract_.insert("method");
  config.initializeDefaults();

  config.group_metadata_key_ = proto.group_metadata_key();

  // Process method-specific configs (for both extraction rules and group overrides)
  for (const auto& method_proto : proto.methods()) {
    MethodConfigEntry entry;
    entry.method_pattern = method_proto.method();
    entry.group = method_proto.group();

    for (const auto& extraction_rule_proto : method_proto.extraction_rules()) {
      entry.extraction_rules.emplace_back(extraction_rule_proto.path());
    }

    config.method_configs_.push_back(std::move(entry));

    // Also update method_fields_ for extraction rules (for backward compatibility)
    if (!method_proto.extraction_rules().empty()) {
      std::vector<AttributeExtractionRule> extraction_rules;
      for (const auto& rule_proto : method_proto.extraction_rules()) {
        extraction_rules.emplace_back(rule_proto.path());
      }
      config.addMethodConfig(method_proto.method(), std::move(extraction_rules));
    }
  }

  config.buildFieldRequirements();
  return config;
}

McpParserConfig McpParserConfig::createDefault() {
  McpParserConfig config;
  config.initializeDefaults();
  config.buildFieldRequirements();
  return config;
}

const std::vector<McpParserConfig::AttributeExtractionRule>&
McpParserConfig::getFieldsForMethod(const std::string& method) const {
  static const std::vector<AttributeExtractionRule> empty;
  auto it = method_fields_.find(method);
  return (it != method_fields_.end()) ? it->second : empty;
}

const McpParserConfig::FieldRequirements&
McpParserConfig::getFieldRequirementsForMethod(const std::string& method) const {
  auto it = method_requirements_.find(method);
  if (it != method_requirements_.end()) {
    return it->second;
  }
  return default_requirements_;
}

std::string McpParserConfig::getMethodGroup(const std::string& method) const {
  // Check user-configured rules first (exact match only)
  for (const auto& entry : method_configs_) {
    if (method == entry.method_pattern && !entry.group.empty()) {
      return entry.group;
    }
  }

  // Fall back to built-in groups
  return getBuiltInMethodGroup(method);
}

std::string McpParserConfig::getBuiltInMethodGroup(const std::string& method) const {
  using namespace McpConstants::Methods;
  using namespace McpConstants::MethodGroups;

  // Lifecycle methods
  if (method == INITIALIZE || method == NOTIFICATION_INITIALIZED || method == PING) {
    return std::string(LIFECYCLE);
  }

  // Tool methods
  if (method == TOOLS_CALL || method == TOOLS_LIST) {
    return std::string(TOOL);
  }

  // Resource methods
  if (method == RESOURCES_READ || method == RESOURCES_LIST || method == RESOURCES_SUBSCRIBE ||
      method == RESOURCES_UNSUBSCRIBE || method == RESOURCES_TEMPLATES_LIST) {
    return std::string(RESOURCE);
  }

  // Prompt methods
  if (method == PROMPTS_GET || method == PROMPTS_LIST) {
    return std::string(PROMPT);
  }

  // Logging
  if (method == LOGGING_SET_LEVEL) {
    return std::string(LOGGING);
  }

  // Sampling
  if (method == SAMPLING_CREATE_MESSAGE) {
    return std::string(SAMPLING);
  }

  // Completion
  if (method == COMPLETION_COMPLETE) {
    return std::string(COMPLETION);
  }

  // General notifications (prefix match, excluding those already categorized)
  if (absl::StartsWith(method, NOTIFICATION_PREFIX)) {
    return std::string(NOTIFICATION);
  }

  return std::string(UNKNOWN);
}

void McpParserConfig::buildFieldRequirements() {
  buildMethodRequirements({}, default_requirements_);

  method_requirements_.clear();
  method_requirements_.reserve(method_fields_.size());
  for (const auto& entry : method_fields_) {
    FieldRequirements requirements;
    buildMethodRequirements(entry.second, requirements);
    method_requirements_.emplace(entry.first, std::move(requirements));
  }
}

void McpParserConfig::buildMethodRequirements(
    const std::vector<AttributeExtractionRule>& method_fields,
    FieldRequirements& requirements) const {
  requirements.required.clear();
  requirements.optional.clear();

  absl::flat_hash_set<std::string> required_set;

  // All method-specific extraction rules are required.
  for (const auto& field : method_fields) {
    if (required_set.insert(field.path).second) {
      requirements.required.push_back(field.path);
    }
  }

  if (!required_set.contains(std::string(McpConstants::kOptionalMetaField))) {
    requirements.optional.push_back(std::string(McpConstants::kOptionalMetaField));
  }
}

// McpFieldExtractor implementation
McpFieldExtractor::McpFieldExtractor(Protobuf::Struct& metadata, const McpParserConfig& config)
    : root_metadata_(metadata), config_(config) {
  // Start with temp storage
  context_stack_.push({&temp_storage_, ""});

  // Pre-calculate total fields needed for early stop optimization
  required_fields_needed_ = config_.getAlwaysExtract().size();
  fields_needed_ = required_fields_needed_;
}

McpFieldExtractor* McpFieldExtractor::StartObject(absl::string_view name) {
  if (can_stop_parsing_) {
    return this;
  }

  // Skip arrays
  if (array_depth_ > 0) {
    return this;
  }

  depth_++;

  if (!name.empty()) {
    path_stack_.push_back(std::string(name));
    // Update cached path
    if (!current_path_cache_.empty()) {
      current_path_cache_ += ".";
    }
    current_path_cache_ += name;

    // Track when we enter the "params" object (direct child of root)
    if (depth_ == 2 && name == "params") {
      params_depth_ = depth_;
    }
  }

  auto* parent = context_stack_.top().struct_ptr;
  if (parent && !name.empty()) {
    // Create nested structure in temp storage
    auto* nested = (*parent->mutable_fields())[std::string(name)].mutable_struct_value();
    context_stack_.push({nested, std::string(name)});
  } else if (depth_ == 1) {
    // Root object
    context_stack_.push({&temp_storage_, ""});
  }

  return this;
}

McpFieldExtractor* McpFieldExtractor::EndObject() {
  if (array_depth_ > 0) {
    return this;
  }

  if (depth_ > 0) {
    // Before updating path, mark object path as collected for early-stop optimization.
    // This enables extraction rules targeting object paths (e.g., "params.ref") to work
    // with early termination, since objects themselves are not rendered as primitives.
    if (!current_path_cache_.empty()) {
      if (collected_fields_.insert(current_path_cache_).second) {
        fields_collected_count_++;
      }
    }

    depth_--;

    // Check if we just exited the "params" object using depth tracking
    if (params_depth_ > 0 && depth_ < params_depth_) {
      params_depth_ = 0; // Reset - we've exited params
      checkEarlyStop();
    }

    if (!path_stack_.empty()) {
      // Update cached path before removing from stack
      size_t last_dot = current_path_cache_.rfind('.');
      if (last_dot != std::string::npos) {
        current_path_cache_.resize(last_dot);
      } else {
        current_path_cache_.clear();
      }
      path_stack_.pop_back();
    }
    if (context_stack_.size() > 1) {
      context_stack_.pop();
    }
  }

  // When we finish the root object, do selective extraction
  if (depth_ == 0 && !can_stop_parsing_) {
    finalizeExtraction();
  }

  return this;
}

McpFieldExtractor* McpFieldExtractor::StartList(absl::string_view) {
  // Arrays not supported - skip
  array_depth_++;
  return this;
}

McpFieldExtractor* McpFieldExtractor::EndList() {
  if (array_depth_ > 0) {
    array_depth_--;
  }
  return this;
}

std::string McpFieldExtractor::buildFullPath(absl::string_view name) const {
  std::string full_path;
  if (!name.empty()) {
    if (!current_path_cache_.empty()) {
      full_path.reserve(current_path_cache_.size() + 1 + name.size());
      full_path = current_path_cache_;
      full_path += ".";
      full_path += name;
    } else {
      full_path = std::string(name);
    }
  } else {
    full_path = current_path_cache_;
  }
  return full_path;
}

McpFieldExtractor* McpFieldExtractor::RenderString(absl::string_view name,
                                                   absl::string_view value) {
  if (can_stop_parsing_ || array_depth_ > 0) {
    return this;
  }

  std::string full_path = buildFullPath(name);
  ENVOY_LOG_MISC(debug, "render string name {} path {}, value {}", name, full_path, value);

  // Check top-level fields for method detection
  if (depth_ == 1) {
    if (name == JSONRPC_FIELD && value == JSONRPC_VERSION) {
      has_jsonrpc_ = true;
      if (has_method_) {
        is_valid_mcp_ = true;
      }
    } else if (name == METHOD_FIELD) {
      has_method_ = true;
      if (has_jsonrpc_) {
        is_valid_mcp_ = true;
      }
      method_ = std::string(value);
      is_notification_ = absl::StartsWith(method_, Methods::NOTIFICATION_PREFIX);
    }
  }

  // Store in temp storage
  Protobuf::Value proto_value;
  proto_value.set_string_value(std::string(value));
  storeField(full_path, proto_value);

  // Check for early stop
  checkEarlyStop();
  return this;
}

McpFieldExtractor* McpFieldExtractor::RenderBool(absl::string_view name, bool value) {
  if (can_stop_parsing_ || array_depth_ > 0) {
    return this;
  }

  std::string full_path = buildFullPath(name);

  Protobuf::Value proto_value;
  proto_value.set_bool_value(value);
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

McpFieldExtractor* McpFieldExtractor::RenderInt32(absl::string_view name, int32_t value) {
  return RenderInt64(name, static_cast<int64_t>(value));
}

McpFieldExtractor* McpFieldExtractor::RenderUint32(absl::string_view name, uint32_t value) {
  return RenderUint64(name, static_cast<uint64_t>(value));
}

McpFieldExtractor* McpFieldExtractor::RenderInt64(absl::string_view name, int64_t value) {
  if (can_stop_parsing_ || array_depth_ > 0) {
    return this;
  }

  std::string full_path = buildFullPath(name);

  Protobuf::Value proto_value;
  proto_value.set_number_value(static_cast<double>(value));
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

McpFieldExtractor* McpFieldExtractor::RenderUint64(absl::string_view name, uint64_t value) {
  if (can_stop_parsing_ || array_depth_ > 0) {
    return this;
  }

  std::string full_path = buildFullPath(name);

  Protobuf::Value proto_value;
  proto_value.set_number_value(static_cast<double>(value));
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

McpFieldExtractor* McpFieldExtractor::RenderDouble(absl::string_view name, double value) {
  if (can_stop_parsing_ || array_depth_ > 0) {
    return this;
  }

  std::string full_path = buildFullPath(name);

  Protobuf::Value proto_value;
  proto_value.set_number_value(value);
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

McpFieldExtractor* McpFieldExtractor::RenderFloat(absl::string_view name, float value) {
  return RenderDouble(name, static_cast<double>(value));
}

McpFieldExtractor* McpFieldExtractor::RenderNull(absl::string_view name) {
  if (can_stop_parsing_ || array_depth_ > 0) {
    return this;
  }

  std::string full_path = buildFullPath(name);

  Protobuf::Value proto_value;
  proto_value.set_null_value(Protobuf::NULL_VALUE);
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

McpFieldExtractor* McpFieldExtractor::RenderBytes(absl::string_view name, absl::string_view value) {
  return RenderString(name, value);
}

void McpFieldExtractor::storeField(const std::string& path, const Protobuf::Value& value) {
  // Store in nested structure in temp storage
  if (!context_stack_.empty() && context_stack_.top().struct_ptr) {
    auto* current = context_stack_.top().struct_ptr;
    size_t last_dot = path.rfind('.');
    std::string field_name = (last_dot != std::string::npos) ? path.substr(last_dot + 1) : path;
    if (!field_name.empty()) {
      (*current->mutable_fields())[field_name] = value;
    }
  }

  // Track new fields for early stop optimization
  if (collected_fields_.insert(path).second) {
    fields_collected_count_++;
  }
}

void McpFieldExtractor::checkEarlyStop() {
  // Can't stop if we haven't seen the method yet
  if (!has_jsonrpc_ || !has_method_) {
    return;
  }

  updateFieldRequirements();

  // Fast path: check if we have collected enough fields
  if (fields_collected_count_ < required_fields_needed_) {
    return; // Still missing required fields
  }

  if (!requiredFieldsCollected()) {
    return;
  }

  // No optional fields configured - can stop now.
  if (!has_optional_fields_) {
    can_stop_parsing_ = true;
    ENVOY_LOG(debug, "early stop: Have all required fields for method {}", method_);
    return;
  }

  // Check if we've collected all optional fields
  bool all_optional_collected = true;
  for (const auto& field : optional_fields_) {
    if (collected_fields_.count(field) == 0) {
      all_optional_collected = false;
      break;
    }
  }

  if (all_optional_collected) {
    can_stop_parsing_ = true;
    ENVOY_LOG(debug, "early stop: Have all required + optional fields for method {}", method_);
    return;
  }

  // If we're currently inside the params object, we must continue parsing because
  // optional fields (like params._meta) may still appear.
  if (params_depth_ > 0) {
    ENVOY_LOG(debug, "still inside params object (depth={}), waiting for optional fields",
              params_depth_);
    return;
  }

  // params_depth_ == 0 means: either we never entered params, or we already exited it.
  // In either case, params.* optional fields can't appear anymore.
  can_stop_parsing_ = true;
  ENVOY_LOG(debug,
            "early stop: params object exited or not present - optional fields cannot appear");
}

void McpFieldExtractor::updateFieldRequirements() {
  if (fields_needed_updated_) {
    return;
  }

  const auto& requirements = config_.getFieldRequirementsForMethod(method_);
  required_fields_ = requirements.required;
  optional_fields_ = requirements.optional;

  required_fields_needed_ = config_.getAlwaysExtract().size() + required_fields_.size();
  fields_needed_ = required_fields_needed_ + optional_fields_.size();

  // Notifications don't have 'id' field, so reduce the expected count
  if (is_notification_) {
    if (required_fields_needed_ > 0) {
      required_fields_needed_--;
    }
    if (fields_needed_ > 0) {
      fields_needed_--;
    }
  }

  has_optional_fields_ = !optional_fields_.empty();
  fields_needed_updated_ = true;
}

bool McpFieldExtractor::requiredFieldsCollected() const {
  // Verify we actually have all required fields (not just the count)
  // Notifications don't have an 'id' field per JSON-RPC spec, so skip it for them
  for (const auto& field : config_.getAlwaysExtract()) {
    if (is_notification_ && field == "id") {
      continue;
    }
    if (collected_fields_.count(field) == 0) {
      return false;
    }
  }

  for (const auto& field : required_fields_) {
    if (collected_fields_.count(field) == 0) {
      return false;
    }
  }

  return true;
}

void McpFieldExtractor::finalizeExtraction() {
  if (!has_jsonrpc_ || !has_method_) {
    ENVOY_LOG(debug, "not a valid MCP message");
    is_valid_mcp_ = false;
    return;
  }

  // Copy selected fields from temp to final
  copySelectedFields();

  // Validate required fields
  validateRequiredFields();
}

bool McpFieldExtractor::hasOptionalFields() {
  if (!has_method_) {
    return false;
  }
  updateFieldRequirements();
  return has_optional_fields_;
}

bool McpFieldExtractor::hasAllRequiredFields() {
  if (!has_jsonrpc_ || !has_method_) {
    return false;
  }
  updateFieldRequirements();
  return requiredFieldsCollected();
}

void McpFieldExtractor::copySelectedFields() {
  absl::flat_hash_set<std::string> copied_fields;

  for (const auto& field : config_.getAlwaysExtract()) {
    if (copied_fields.insert(field).second) {
      copyFieldByPath(field);
    }
  }

  // Copy method-specific fields
  const auto& fields = config_.getFieldsForMethod(method_);
  for (const auto& field : fields) {
    if (copied_fields.insert(field.path).second) {
      copyFieldByPath(field.path);
    }
  }

  const std::string meta_field(McpConstants::kOptionalMetaField);
  if (copied_fields.insert(meta_field).second) {
    copyFieldByPath(meta_field);
  }
}

void McpFieldExtractor::copyFieldByPath(const std::string& path) {
  std::vector<std::string> segments = absl::StrSplit(path, '.');

  // Navigate source to find value
  const Protobuf::Struct* current_source = &temp_storage_;
  const Protobuf::Value* value = nullptr;

  for (size_t i = 0; i < segments.size(); ++i) {
    auto it = current_source->fields().find(segments[i]);
    if (it == current_source->fields().end()) {
      return; // Field doesn't exist
    }

    if (i == segments.size() - 1) {
      value = &it->second;
    } else {
      if (!it->second.has_struct_value()) {
        return;
      }
      current_source = &it->second.struct_value();
    }
  }

  if (!value) {
    return;
  }

  // Navigate dest and create nested structure
  Protobuf::Struct* current_dest = &root_metadata_;

  for (size_t i = 0; i < segments.size() - 1; ++i) {
    auto& fields = *current_dest->mutable_fields();
    auto it = fields.find(segments[i]);

    if (it == fields.end() || !it->second.has_struct_value()) {
      current_dest = fields[segments[i]].mutable_struct_value();
    } else {
      current_dest = it->second.mutable_struct_value();
    }
  }

  // Copy the final value
  (*current_dest->mutable_fields())[segments.back()] = *value;
  extracted_fields_.insert(path);
}

void McpFieldExtractor::validateRequiredFields() {
  updateFieldRequirements();
  for (const auto& field : required_fields_) {
    if (extracted_fields_.count(field) == 0) {
      missing_required_fields_.push_back(field);
      ENVOY_LOG(debug, "missing required field for {}: {}", method_, field);
    }
  }
}

// McpJsonParser implementation
McpJsonParser::McpJsonParser(const McpParserConfig& config) : config_(config) { reset(); }

absl::Status McpJsonParser::parse(absl::string_view data) {
  if (!parsing_started_) {
    extractor_ = std::make_unique<McpFieldExtractor>(metadata_, config_);
    stream_parser_ = std::make_unique<ProtobufUtil::converter::JsonStreamParser>(extractor_.get());
    parsing_started_ = true;
  }

  auto status = stream_parser_->Parse(data);
  ENVOY_LOG(trace, "status ok: {}, {}", status.ok(), status.message());
  if (extractor_->shouldStopParsing()) {
    ENVOY_LOG(trace, "Parser stopped early - all required fields collected");
    all_fields_collected_ = true;
    return finishParse();
  }
  return status;
}

absl::Status McpJsonParser::finishParse() {
  if (!parsing_started_) {
    return absl::InvalidArgumentError("No data has been parsed");
  }
  ENVOY_LOG(debug, "parser finishParse");
  auto status = stream_parser_->FinishParse();
  extractor_->finalizeExtraction();
  return status;
}

bool McpJsonParser::isValidMcpRequest() const { return extractor_ && extractor_->isValidMcp(); }

bool McpJsonParser::hasOptionalFields() { return extractor_ && extractor_->hasOptionalFields(); }

bool McpJsonParser::hasAllRequiredFields() {
  return extractor_ && extractor_->hasAllRequiredFields();
}

const std::string& McpJsonParser::getMethod() const {
  static const std::string empty;
  return extractor_ ? extractor_->getMethod() : empty;
}

const Protobuf::Value* McpJsonParser::getNestedValue(const std::string& dotted_path) const {
  if (dotted_path.empty()) {
    return nullptr;
  }

  std::vector<std::string> path = absl::StrSplit(dotted_path, '.');
  const Protobuf::Struct* current = &metadata_;

  for (size_t i = 0; i < path.size(); ++i) {
    auto it = current->fields().find(path[i]);
    if (it == current->fields().end()) {
      return nullptr;
    }

    if (i == path.size() - 1) {
      return &it->second;
    } else {
      if (!it->second.has_struct_value()) {
        return nullptr;
      }
      current = &it->second.struct_value();
    }
  }

  return nullptr;
}

void McpJsonParser::reset() {
  metadata_.Clear();
  extractor_.reset();
  stream_parser_.reset();
  parsing_started_ = false;
}

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
