#include "source/extensions/filters/http/mcp/mcp_json_parser.h"
#include "source/common/common/assert.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

// Rule implementation - updated constructor
Rule::Rule(const std::string& path, const std::string& metadata_key) {
  // Parse path into segments
  path_segments = absl::StrSplit(path, '.', absl::SkipEmpty());

  // Set key - if empty, use the full path as key
  if (metadata_key.empty()) {
    key = path;  // Use full path as default, not just last segment
  } else {
    key = metadata_key;
  }
}

// ParserConfig implementation
ParserConfig ParserConfig::fromProto(
    const envoy::extensions::filters::http::mcp::v3::ParserConfig& proto) {
  ParserConfig config;

  for (const auto& rule : proto.rules()) {
    if (rule.path() == "jsonrpc") {
      continue;
    }
    config.rules.emplace_back(rule.path(), rule.key());
  }

  // The parser will by default extract the json_rpc;
  config.rules.emplace_back("jsonrpc", "");

  return config;
}

ParserConfig ParserConfig::defaultMcpConfig() {
  ParserConfig config;
  config.rules.emplace_back("jsonrpc", "");
  config.rules.emplace_back("method", "");
  return config;
}

// FieldExtractorObjectWriter implementation
FieldExtractorObjectWriter::FieldExtractorObjectWriter(
    Protobuf::Struct& metadata, const ParserConfig& config)
    : metadata_(metadata), config_(config), 
      matched_rules_(config.rules.size(), false) {}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::StartObject(absl::string_view name) {
  if (shouldStop() || array_depth_ > 0) {
    return this;
  }
  
  depth_++;

  if (!name.empty() && depth_ > 1) {
    pushPath(name);
  }
  
  return this;
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::EndObject() {
  if (array_depth_ > 0) {
    return this;
  }
  
  if (depth_ > 0) {
    depth_--;
    if (!path_.empty()) {
      popPath();
    }
  }
  
  return this;
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::StartList(absl::string_view) {
  // Skip arrays - we don't support them
  array_depth_++;
  return this;
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::EndList() {
  if (array_depth_ > 0) {
    array_depth_--;
  }
  return this;
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::RenderString(
    absl::string_view name, absl::string_view value) {
  if (shouldStop() || array_depth_ > 0) {
    return this;
  }
  
  checkAndExtract(name, std::string(value));
  return this;
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::RenderInt32(
    absl::string_view name, int32_t value) {
  return RenderInt64(name, static_cast<int64_t>(value));
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::RenderUint32(
    absl::string_view name, uint32_t value) {
  return RenderUint64(name, static_cast<uint64_t>(value));
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::RenderInt64(
    absl::string_view name, int64_t value) {
  if (shouldStop() || array_depth_ > 0) {
    return this;
  }
  
  checkAndExtract(name, std::to_string(value));
  return this;
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::RenderUint64(
    absl::string_view name, uint64_t value) {
  if (shouldStop() || array_depth_ > 0) {
    return this;
  }
  
  checkAndExtract(name, std::to_string(value));
  return this;
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::RenderDouble(
    absl::string_view name, double value) {
  if (shouldStop() || array_depth_ > 0) {
    return this;
  }
  
  checkAndExtract(name, std::to_string(value));
  return this;
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::RenderFloat(
    absl::string_view name, float value) {
  return RenderDouble(name, static_cast<double>(value));
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::RenderBool(
    absl::string_view name, bool value) {
  if (shouldStop() || array_depth_ > 0) {
    return this;
  }

  checkAndExtract(name, value ? "true" : "false");
  return this;
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::RenderBytes(
    absl::string_view name, absl::string_view value) {
  return RenderString(name, value);
}

FieldExtractorObjectWriter* FieldExtractorObjectWriter::RenderNull(absl::string_view name) {
  if (shouldStop() || array_depth_ > 0) {
    return this;
  }

  checkAndExtract(name, "");
  return this;
}

void FieldExtractorObjectWriter::checkAndExtract(
    absl::string_view name, const std::string& value) {
  std::vector<std::string> current_path = path_;
  if (!name.empty()) {
    current_path.push_back(std::string(name));
  }

  // Check against all rules
  for (size_t i = 0; i < config_.rules.size(); ++i) {
    if (matched_rules_[i]) {
      continue;
    }

    const auto& rule = config_.rules[i];
    
    // Check if current path matches rule path
    if (current_path.size() == rule.path_segments.size()) {
      bool matches = true;
      for (size_t j = 0; j < current_path.size(); ++j) {
        if (current_path[j] != rule.path_segments[j]) {
          matches = false;
          break;
        }
      }

      if (matches) {
        auto* value_proto = metadata_.mutable_fields()->operator[](rule.key).mutable_string_value();
        *value_proto = value;
        matched_rules_[i] = true;
        
        ENVOY_LOG(debug, "Extracted field {} = {} -> metadata[{}]",
                  absl::StrJoin(current_path, "."), value, rule.key);
      }
    }
  }
}

void FieldExtractorObjectWriter::pushPath(absl::string_view name) {
  path_.push_back(std::string(name));
}

void FieldExtractorObjectWriter::popPath() {
  if (!path_.empty()) {
    path_.pop_back();
  }
}

bool FieldExtractorObjectWriter::shouldStop() {
  if (all_rules_matched_) {
    return true;
  }

   // Check if all rules have been matched
  for (bool matched : matched_rules_) {
    if (!matched) {
      return false;
    }
  }
  all_rules_matched_ = true;
  return true;  // All rules matched
}

// JsonPathParser implementation
JsonPathParser::JsonPathParser(const ParserConfig& config) 
    : config_(config) {
  reset();
}

absl::Status JsonPathParser::parse(absl::string_view data) {
  if (!parsing_started_) {
    object_writer_ = std::make_unique<FieldExtractorObjectWriter>(metadata_, config_);
    stream_parser_ = std::make_unique<google::protobuf::util::converter::JsonStreamParser>(
        object_writer_.get());
    parsing_started_ = true;
  }

  // Apply size limit
  size_t bytes_to_parse = data.size();
  absl::string_view chunk = data.substr(0, bytes_to_parse);
  absl::Status status = stream_parser_->Parse(chunk);
  
  total_bytes_parsed_ += bytes_to_parse;

  if (object_writer_->shouldStop()) {
    ENVOY_LOG(debug, "Stopping JSON parsing - all fields extracted");
    all_rules_matched_ = true;
    return absl::OkStatus();
  }

  return status;
}

absl::Status JsonPathParser::finishParse() {
  if (!parsing_started_) {
    return absl::InvalidArgumentError("No data has been parsed");
  }

  if (object_writer_->shouldStop()) {
    return absl::OkStatus();
  }

  return stream_parser_->FinishParse();
}

bool JsonPathParser::isValidMcpRequest() const {
  const auto& fields = metadata_.fields();
  
  // Check jsonrpc = "2.0"
  auto jsonrpc_it = fields.find("jsonrpc");
  if (jsonrpc_it != fields.end() && 
      jsonrpc_it->second.has_string_value() &&
      jsonrpc_it->second.string_value() == "2.0") {
    return true;
  }

  return false;
}

void JsonPathParser::reset() {
  metadata_.Clear();
  object_writer_.reset();
  stream_parser_.reset();
  parsing_started_ = false;
  total_bytes_parsed_ = 0;
}

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
