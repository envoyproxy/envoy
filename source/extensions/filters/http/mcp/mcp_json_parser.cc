#include "source/extensions/filters/http/mcp/mcp_json_parser.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

using namespace McpConstants;

// Static method to parse method string to enum
McpMethodType McpJsonParser::parseMethod(const std::string& method) {
  using namespace McpConstants::Methods;

  // Tools
  if (method == TOOLS_CALL)
    return McpMethodType::TOOLS_CALL;
  if (method == TOOLS_LIST)
    return McpMethodType::TOOLS_LIST;

  // Resources
  if (method == RESOURCES_READ)
    return McpMethodType::RESOURCES_READ;
  if (method == RESOURCES_LIST)
    return McpMethodType::RESOURCES_LIST;
  if (method == RESOURCES_SUBSCRIBE)
    return McpMethodType::RESOURCES_SUBSCRIBE;
  if (method == RESOURCES_UNSUBSCRIBE)
    return McpMethodType::RESOURCES_UNSUBSCRIBE;
  if (method == RESOURCES_TEMPLATES_LIST)
    return McpMethodType::RESOURCES_TEMPLATES_LIST;

  // Prompts
  if (method == PROMPTS_GET)
    return McpMethodType::PROMPTS_GET;
  if (method == PROMPTS_LIST)
    return McpMethodType::PROMPTS_LIST;

  // Completion
  if (method == COMPLETION_COMPLETE)
    return McpMethodType::COMPLETION_COMPLETE;

  // Logging
  if (method == LOGGING_SET_LEVEL)
    return McpMethodType::LOGGING_SET_LEVEL;

  // Lifecycle
  if (method == INITIALIZE)
    return McpMethodType::INITIALIZE;
  if (method == INITIALIZED)
    return McpMethodType::INITIALIZED;
  if (method == SHUTDOWN)
    return McpMethodType::SHUTDOWN;

  // Sampling
  if (method == SAMPLING_CREATE_MESSAGE)
    return McpMethodType::SAMPLING_CREATE_MESSAGE;

  // Utility
  if (method == PING)
    return McpMethodType::PING;

  // Notifications - check prefix
  if (method.starts_with(std::string(NOTIFICATION_PREFIX))) {
    // Specific notification types
    if (method == NOTIFICATION_RESOURCES_LIST_CHANGED)
      return McpMethodType::NOTIFICATION_RESOURCES_LIST_CHANGED;
    if (method == NOTIFICATION_RESOURCES_UPDATED)
      return McpMethodType::NOTIFICATION_RESOURCES_UPDATED;
    if (method == NOTIFICATION_TOOLS_LIST_CHANGED)
      return McpMethodType::NOTIFICATION_TOOLS_LIST_CHANGED;
    if (method == NOTIFICATION_PROMPTS_LIST_CHANGED)
      return McpMethodType::NOTIFICATION_PROMPTS_LIST_CHANGED;
    if (method == NOTIFICATION_PROGRESS)
      return McpMethodType::NOTIFICATION_PROGRESS;
    if (method == NOTIFICATION_MESSAGE)
      return McpMethodType::NOTIFICATION_MESSAGE;
    if (method == NOTIFICATION_CANCELLED)
      return McpMethodType::NOTIFICATION_CANCELLED;
    if (method == NOTIFICATION_INITIALIZED)
      return McpMethodType::NOTIFICATION_INITIALIZED;
    return McpMethodType::NOTIFICATION_GENERIC;
  }

  return McpMethodType::UNKNOWN;
}

// McpParserConfig implementation
void McpParserConfig::initializeDefaults() {
  // Always extract core JSON-RPC fields
  always_extract_.insert("jsonrpc");
  always_extract_.insert("method");

  // tools/call - only tool name
  method_fields_[McpMethodType::TOOLS_CALL] = {
      FieldPolicy("params.name") // Required
  };

  // tools/list - cursor for pagination
  method_fields_[McpMethodType::TOOLS_LIST] = {FieldPolicy("params.cursor")};

  // resources/read - URI is required
  method_fields_[McpMethodType::RESOURCES_READ] = {FieldPolicy("params.uri")};

  // resources/list - cursor
  method_fields_[McpMethodType::RESOURCES_LIST] = {FieldPolicy("params.cursor")};

  // resources/subscribe - URI is required
  method_fields_[McpMethodType::RESOURCES_SUBSCRIBE] = {FieldPolicy("params.uri")};

  // resources/unsubscribe - URI is required
  method_fields_[McpMethodType::RESOURCES_UNSUBSCRIBE] = {FieldPolicy("params.uri")};

  // resources/templates/list - cursor
  method_fields_[McpMethodType::RESOURCES_TEMPLATES_LIST] = {FieldPolicy("params.cursor")};

  // prompts/get - name is required
  method_fields_[McpMethodType::PROMPTS_GET] = {FieldPolicy("params.name")};

  // prompts/list - cursor
  method_fields_[McpMethodType::PROMPTS_LIST] = {FieldPolicy("params.cursor")};

  // completion/complete - ref fields
  method_fields_[McpMethodType::COMPLETION_COMPLETE] = {};

  // logging/setLevel - level is required
  method_fields_[McpMethodType::LOGGING_SET_LEVEL] = {FieldPolicy("params.level")};

  // initialize - protocol version and client info
  method_fields_[McpMethodType::INITIALIZE] = {FieldPolicy("params.protocolVersion"),
                                               FieldPolicy("params.clientInfo.name")};

  // Empty configs for simple methods
  method_fields_[McpMethodType::SAMPLING_CREATE_MESSAGE] = {};
  method_fields_[McpMethodType::INITIALIZED] = {};
  method_fields_[McpMethodType::SHUTDOWN] = {};
  method_fields_[McpMethodType::PING] = {};

  // Notifications
  method_fields_[McpMethodType::NOTIFICATION_RESOURCES_UPDATED] = {FieldPolicy("params.uri")};

  method_fields_[McpMethodType::NOTIFICATION_PROGRESS] = {FieldPolicy("params.progressToken"),
                                                          FieldPolicy("params.progress")};

  method_fields_[McpMethodType::NOTIFICATION_CANCELLED] = {FieldPolicy("params.requestId")};

  method_fields_[McpMethodType::NOTIFICATION_MESSAGE] = {FieldPolicy("params.level")};

  // Other notifications - no params
  method_fields_[McpMethodType::NOTIFICATION_RESOURCES_LIST_CHANGED] = {};
  method_fields_[McpMethodType::NOTIFICATION_TOOLS_LIST_CHANGED] = {};
  method_fields_[McpMethodType::NOTIFICATION_PROMPTS_LIST_CHANGED] = {};
  method_fields_[McpMethodType::NOTIFICATION_INITIALIZED] = {};
  method_fields_[McpMethodType::NOTIFICATION_GENERIC] = {};
}

McpParserConfig McpParserConfig::createDefault() {
  McpParserConfig config;
  config.initializeDefaults();
  // TODO(botengyao) add proto configs
  return config;
}

const std::vector<McpParserConfig::FieldPolicy>&
McpParserConfig::getFieldsForMethod(McpMethodType method) const {
  static const std::vector<FieldPolicy> empty;
  auto it = method_fields_.find(method);
  return (it != method_fields_.end()) ? it->second : empty;
}

// McpFieldExtractor implementation
McpFieldExtractor::McpFieldExtractor(Protobuf::Struct& metadata, const McpParserConfig& config)
    : root_metadata_(metadata), config_(config) {
  // Start with temp storage
  context_stack_.push({&temp_storage_, ""});
}

McpFieldExtractor* McpFieldExtractor::StartObject(absl::string_view name) {
  if (can_stop_parsing_) {
    return this; // Early stop
  }

  if (array_depth_ > 0) {
    return this; // Skip arrays
  }

  depth_++;

  if (!name.empty()) {
    path_stack_.push_back(std::string(name));
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
  if (depth_ > 0) {
    depth_--;
    if (!path_stack_.empty()) {
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

McpFieldExtractor* McpFieldExtractor::RenderString(absl::string_view name,
                                                   absl::string_view value) {
  if (can_stop_parsing_) {
    return this;
  }

  if (array_depth_ > 0) {
    return this;
  }

  std::string full_path = getCurrentPath();
  if (!name.empty()) {
    if (!full_path.empty()) {
      full_path += ".";
    }
    full_path += name;
  }
  ENVOY_LOG_MISC(debug, "render string name {} path {}, value {}", name, full_path, value);

  // Check top-level fields for method detection
  if (depth_ == 1) {
    if (name == JSONRPC_FIELD && value == JSONRPC_VERSION) {
      has_jsonrpc_ = true;
      is_valid_mcp_ = true;
    } else if (name == METHOD_FIELD) {
      has_method_ = true;
      method_ = std::string(value);
      method_type_ = McpJsonParser::parseMethod(method_);
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

  std::string full_path = getCurrentPath();
  if (!name.empty()) {
    if (!full_path.empty())
      full_path += ".";
    full_path += name;
  }

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

  std::string full_path = getCurrentPath();
  if (!name.empty()) {
    if (!full_path.empty())
      full_path += ".";
    full_path += name;
  }

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

  std::string full_path = getCurrentPath();
  if (!name.empty()) {
    if (!full_path.empty())
      full_path += ".";
    full_path += name;
  }

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

  std::string full_path = getCurrentPath();
  if (!name.empty()) {
    if (!full_path.empty())
      full_path += ".";
    full_path += name;
  }

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

  std::string full_path = getCurrentPath();
  if (!name.empty()) {
    if (!full_path.empty())
      full_path += ".";
    full_path += name;
  }

  Protobuf::Value proto_value;
  proto_value.set_null_value(Protobuf::NULL_VALUE);
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

McpFieldExtractor* McpFieldExtractor::RenderBytes(absl::string_view name, absl::string_view value) {
  return RenderString(name, value);
}

std::string McpFieldExtractor::getCurrentPath() const { return absl::StrJoin(path_stack_, "."); }

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

  collected_fields_.insert(path);
}

void McpFieldExtractor::checkEarlyStop() {
  // Can't stop if we haven't seen jsonrpc and method yet
  ENVOY_LOG(debug, "boteng checkEarlyStop");
  if (!has_jsonrpc_ || !has_method_) {
    return;
  }

  // Check if we have all global fields we need
  for (const auto& field : config_.getAlwaysExtract()) {
    if (collected_fields_.count(field) == 0) {
      return;
    }
  }

  // For requests/notifications, check method-specific fields
  const auto& required_fields = config_.getFieldsForMethod(method_type_);
  for (const auto& field : required_fields) {
    if (collected_fields_.count(field.path) == 0) {
      return; // Still missing this field
    }
  }

  // We have everything we need
  can_stop_parsing_ = true;
  ENVOY_LOG(debug, "early stop: Have all fields for method {}", method_);
}

void McpFieldExtractor::finalizeExtraction() {
  ENVOY_LOG_MISC(trace, "calling finalizeExtraction");
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
  if (method_type_ != McpMethodType::UNKNOWN) {
    const auto& fields = config_.getFieldsForMethod(method_type_);
    for (const auto& field : fields) {
      copyFieldByPath(field.path);
    }
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
  if (method_type_ == McpMethodType::UNKNOWN) {
    return;
  }

  const auto& fields = config_.getFieldsForMethod(method_type_);
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
    stream_parser_ =
        std::make_unique<google::protobuf::util::converter::JsonStreamParser>(extractor_.get());
    parsing_started_ = true;
  }

  auto status = stream_parser_->Parse(data);
  ENVOY_LOG(debug, "parser pasing");
  if (extractor_->shouldStopParsing()) {
    ENVOY_LOG(debug, "Parser stopped early - all required fields collected");
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

McpMethodType McpJsonParser::getMethodType() const {
  return extractor_ ? extractor_->getMethodType() : McpMethodType::UNKNOWN;
}

const std::string& McpJsonParser::getMethod() const {
  static const std::string empty;
  return extractor_ ? extractor_->getMethod() : empty;
}

const std::vector<std::string>& McpJsonParser::getMissingRequiredFields() const {
  static const std::vector<std::string> empty;
  return extractor_ ? extractor_->getMissingRequiredFields() : empty;
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
