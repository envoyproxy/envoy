#include "source/common/json/json_rpc_field_extractor.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Json {

JsonRpcFieldExtractor::JsonRpcFieldExtractor(Protobuf::Struct& metadata,
                                             const JsonRpcParserConfig& config)
    : root_metadata_(metadata), config_(config) {
  // Start with temp storage
  context_stack_.push({&temp_storage_, nullptr, ""});

  // Pre-calculate total fields needed for early stop optimization
  fields_needed_ = config_.getAlwaysExtract().size();
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::StartObject(absl::string_view name) {
  if (array_depth_ > 0 || can_stop_parsing_) {
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

  if (context_stack_.top().is_list()) {
    auto* nested = context_stack_.top().list_ptr->add_values()->mutable_struct_value();
    context_stack_.push({nested, nullptr, ""});
  } else if (context_stack_.top().struct_ptr) {
    if (!name.empty()) {
      auto* nested = (*context_stack_.top().struct_ptr->mutable_fields())[std::string(name)]
                         .mutable_struct_value();
      context_stack_.push({nested, nullptr, std::string(name)});
    } else if (depth_ == 1) {
      context_stack_.push({&temp_storage_, nullptr, ""});
    }
  }
  return this;
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::EndObject() {
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

JsonRpcFieldExtractor* JsonRpcFieldExtractor::StartList(absl::string_view name) {
  if (!lists_supported()) {
    array_depth_++;
    return this;
  }
  if (can_stop_parsing_) {
    return this;
  }

  if (!name.empty()) {
    path_stack_.push_back(std::string(name));
    if (!current_path_cache_.empty()) {
      current_path_cache_ += ".";
    }
    current_path_cache_ += name;
  }

  if (context_stack_.top().is_list()) {
    auto* list = context_stack_.top().list_ptr->add_values()->mutable_list_value();
    context_stack_.push({nullptr, list, ""});
  } else if (context_stack_.top().struct_ptr) {
    auto* list = (*context_stack_.top().struct_ptr->mutable_fields())[std::string(name)]
                     .mutable_list_value();
    context_stack_.push({nullptr, list, std::string(name)});
  }

  return this;
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::EndList() {
  if (!lists_supported()) {
    if (array_depth_ > 0) {
      array_depth_--;
    }
    return this;
  }
  if (!path_stack_.empty() && context_stack_.top().is_list()) {
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
  return this;
}

std::string JsonRpcFieldExtractor::buildFullPath(absl::string_view name) const {
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

JsonRpcFieldExtractor* JsonRpcFieldExtractor::RenderString(absl::string_view name,
                                                           absl::string_view value) {
  if (array_depth_ > 0 || can_stop_parsing_) {
    return this;
  }
  std::string full_path = buildFullPath(name);
  ENVOY_LOG_MISC(debug, "render string name {} path {}, value {}", name, full_path, value);

  // Check top-level fields for method detection
  if (depth_ == 1) {
    if (name == jsonRpcField() && value == jsonRpcVersion()) {
      has_jsonrpc_ = true;
      if (has_method_) {
        is_valid_jsonrpc_ = true;
      }
    } else if (name == methodField()) {
      has_method_ = true;
      if (has_jsonrpc_) {
        is_valid_jsonrpc_ = true;
      }
      method_ = std::string(value);
      is_notification_ = isNotification(method_);
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

JsonRpcFieldExtractor* JsonRpcFieldExtractor::RenderBool(absl::string_view name, bool value) {
  if (array_depth_ > 0) {
    return this;
  }
  if (can_stop_parsing_) {
    return this;
  }

  std::string full_path = buildFullPath(name);

  Protobuf::Value proto_value;
  proto_value.set_bool_value(value);
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::RenderInt32(absl::string_view name, int32_t value) {
  return RenderInt64(name, static_cast<int64_t>(value));
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::RenderUint32(absl::string_view name, uint32_t value) {
  return RenderUint64(name, static_cast<uint64_t>(value));
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::RenderInt64(absl::string_view name, int64_t value) {
  if (array_depth_ > 0) {
    return this;
  }
  if (can_stop_parsing_) {
    return this;
  }

  std::string full_path = buildFullPath(name);

  Protobuf::Value proto_value;
  proto_value.set_number_value(static_cast<double>(value));
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::RenderUint64(absl::string_view name, uint64_t value) {
  if (array_depth_ > 0) {
    return this;
  }
  if (can_stop_parsing_) {
    return this;
  }

  std::string full_path = buildFullPath(name);

  Protobuf::Value proto_value;
  proto_value.set_number_value(static_cast<double>(value));
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::RenderDouble(absl::string_view name, double value) {
  if (array_depth_ > 0) {
    return this;
  }
  if (can_stop_parsing_) {
    return this;
  }

  std::string full_path = buildFullPath(name);

  Protobuf::Value proto_value;
  proto_value.set_number_value(value);
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::RenderFloat(absl::string_view name, float value) {
  return RenderDouble(name, static_cast<double>(value));
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::RenderNull(absl::string_view name) {
  if (array_depth_ > 0) {
    return this;
  }
  if (can_stop_parsing_) {
    return this;
  }

  std::string full_path = buildFullPath(name);

  Protobuf::Value proto_value;
  proto_value.set_null_value(Protobuf::NULL_VALUE);
  storeField(full_path, proto_value);

  checkEarlyStop();
  return this;
}

JsonRpcFieldExtractor* JsonRpcFieldExtractor::RenderBytes(absl::string_view name,
                                                          absl::string_view value) {
  return RenderString(name, value);
}

void JsonRpcFieldExtractor::storeField(const std::string& path, const Protobuf::Value& value) {
  if (context_stack_.empty()) {
    return;
  }
  // Store in nested structure in temp storage
  if (context_stack_.top().is_list()) {
    *context_stack_.top().list_ptr->add_values() = value;
  } else if (context_stack_.top().struct_ptr) {
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

void JsonRpcFieldExtractor::checkEarlyStop() {
  // Can't stop if we haven't seen the method yet
  if (!has_jsonrpc_ || !has_method_) {
    return;
  }

  // Update fields_needed_ now that we know the method
  if (!fields_needed_updated_) {
    const auto& required_fields = config_.getFieldsForMethod(method_);
    fields_needed_ += required_fields.size();
    // Notifications don't have 'id' field, so reduce the expected count
    if (is_notification_) {
      fields_needed_--;
    }
    fields_needed_updated_ = true;
  }

  // Fast path: check if we have collected enough fields
  if (fields_collected_count_ < fields_needed_) {
    return; // Still missing fields
  }

  // Verify we actually have all required fields (not just the count)
  // Notifications don't have an 'id' field per JSON-RPC spec, so skip it for them
  for (const auto& field : config_.getAlwaysExtract()) {
    if (is_notification_ && field == "id") {
      continue;
    }
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

void JsonRpcFieldExtractor::finalizeExtraction() {
  if (!has_jsonrpc_ || !has_method_) {
    ENVOY_LOG(debug, "not a valid {} message", protocolName());
    is_valid_jsonrpc_ = false;
    return;
  }

  // Copy selected fields from temp to final
  copySelectedFields();

  // Validate required fields
  validateRequiredFields();
}

void JsonRpcFieldExtractor::copySelectedFields() {
  for (const auto& field : config_.getAlwaysExtract()) {
    copyFieldByPath(field);
  }

  // Copy method-specific fields
  const auto& fields = config_.getFieldsForMethod(method_);
  for (const auto& field : fields) {
    copyFieldByPath(field.path);
  }
}

void JsonRpcFieldExtractor::copyFieldByPath(const std::string& path) {
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
      if (it->second.has_list_value()) {
        // if path segment is list but we have more segments, not supported for copy.
        // TODO(tyxia): support list indexing in path.
        return;
      } else if (!it->second.has_struct_value()) {
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

void JsonRpcFieldExtractor::validateRequiredFields() {
  const auto& fields = config_.getFieldsForMethod(method_);
  for (const auto& field : fields) {
    if (extracted_fields_.count(field.path) == 0) {
      missing_required_fields_.push_back(field.path);
      ENVOY_LOG(debug, "missing required field for {}: {}", method_, field.path);
    }
  }
}

} // namespace Json
} // namespace Envoy
