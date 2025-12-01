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
  method_fields_[std::string(method)] = std::move(fields);
}

void McpParserConfig::initializeDefaults() {
  // Always extract core JSON-RPC fields
  always_extract_.insert("jsonrpc");
  always_extract_.insert("method");

  // Tools
  addMethodConfig(Methods::TOOLS_CALL, {AttributeExtractionRule("params.name")});

  // Resources
  addMethodConfig(Methods::RESOURCES_READ, {AttributeExtractionRule("params.uri")});
  addMethodConfig(Methods::RESOURCES_LIST, {AttributeExtractionRule("params.cursor")});
  addMethodConfig(Methods::RESOURCES_SUBSCRIBE, {AttributeExtractionRule("params.uri")});
  addMethodConfig(Methods::RESOURCES_UNSUBSCRIBE, {AttributeExtractionRule("params.uri")});
  addMethodConfig(Methods::RESOURCES_TEMPLATES_LIST, {AttributeExtractionRule("params.cursor")});

  // Prompts
  addMethodConfig(Methods::PROMPTS_GET, {AttributeExtractionRule("params.name")});
  addMethodConfig(Methods::PROMPTS_LIST, {AttributeExtractionRule("params.cursor")});

  // Completion
  addMethodConfig(Methods::COMPLETION_COMPLETE, {});

  // Logging
  addMethodConfig(Methods::LOGGING_SET_LEVEL, {AttributeExtractionRule("params.level")});

  // Lifecycle
  addMethodConfig(Methods::INITIALIZE, {AttributeExtractionRule("params.protocolVersion"),
                                        AttributeExtractionRule("params.clientInfo.name")});

  // Notifications
  addMethodConfig(Methods::NOTIFICATION_RESOURCES_UPDATED, {AttributeExtractionRule("params.uri")});

  addMethodConfig(Methods::NOTIFICATION_PROGRESS, {AttributeExtractionRule("params.progressToken"),
                                                   AttributeExtractionRule("params.progress")});

  addMethodConfig(Methods::NOTIFICATION_CANCELLED, {AttributeExtractionRule("params.requestId")});

  addMethodConfig(Methods::NOTIFICATION_MESSAGE, {AttributeExtractionRule("params.level")});
}

McpParserConfig
McpParserConfig::fromProto(const envoy::extensions::filters::http::mcp::v3::ParserConfig& proto) {
  McpParserConfig config;

  config.always_extract_.insert("jsonrpc");
  config.always_extract_.insert("method");
  config.initializeDefaults();

  // Process method-specific overrides
  for (const auto& method_proto : proto.methods()) {
    std::vector<AttributeExtractionRule> extraction_rules;
    for (const auto& extraction_rule_proto : method_proto.extraction_rules()) {
      extraction_rules.emplace_back(extraction_rule_proto.path());
    }
    config.addMethodConfig(method_proto.method(), std::move(extraction_rules));
  }

  return config;
}

McpParserConfig McpParserConfig::createDefault() {
  McpParserConfig config;
  config.initializeDefaults();
  return config;
}

const std::vector<McpParserConfig::AttributeExtractionRule>&
McpParserConfig::getFieldsForMethod(const std::string& method) const {
  static const std::vector<AttributeExtractionRule> empty;
  auto it = method_fields_.find(method);
  return (it != method_fields_.end()) ? it->second : empty;
}

// McpFieldExtractor implementation
McpFieldExtractor::McpFieldExtractor(Protobuf::Struct& metadata, const McpParserConfig& config)
    : root_metadata_(metadata), config_(config) {
  // Start with temp storage
  context_stack_.push({&temp_storage_, ""});

  // Pre-calculate total fields needed for early stop optimization
  fields_needed_ = config_.getAlwaysExtract().size();
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
    depth_--;
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

  // Update fields_needed_ now that we know the method
  static bool fields_needed_updated = false;
  if (!fields_needed_updated) {
    const auto& required_fields = config_.getFieldsForMethod(method_);
    fields_needed_ += required_fields.size();
    fields_needed_updated = true;
  }

  // Fast path: check if we have collected enough fields
  if (fields_collected_count_ < fields_needed_) {
    return; // Still missing fields
  }

  // Verify we actually have all required fields (not just the count)
  for (const auto& field : config_.getAlwaysExtract()) {
    if (collected_fields_.count(field) == 0) {
      return;
    }
  }

  const auto& required_fields = config_.getFieldsForMethod(method_);
  for (const auto& field : required_fields) {
    if (collected_fields_.count(field.path) == 0) {
      return;
    }
  }

  can_stop_parsing_ = true;
  ENVOY_LOG(debug, "early stop: Have all fields for method {}", method_);
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

void McpFieldExtractor::copySelectedFields() {
  for (const auto& field : config_.getAlwaysExtract()) {
    copyFieldByPath(field);
  }

  // Copy method-specific fields
  const auto& fields = config_.getFieldsForMethod(method_);
  for (const auto& field : fields) {
    copyFieldByPath(field.path);
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
  const auto& fields = config_.getFieldsForMethod(method_);
  for (const auto& field : fields) {
    if (extracted_fields_.count(field.path) == 0) {
      missing_required_fields_.push_back(field.path);
      ENVOY_LOG(debug, "missing required field for {}: {}", method_, field.path);
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
