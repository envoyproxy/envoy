#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"
#include "google/protobuf/util/converter/json_stream_parser.h"
#include "google/protobuf/util/converter/object_writer.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

/**
 * Extraction rule
 */
struct Rule {
  std::vector<std::string> path_segments;
  std::string key;
  
  Rule(const std::string& path, const std::string& metadata_key);
};

/**
 * Parser configuration
 */
struct ParserConfig {
  std::vector<Rule> rules;
  
  // Build from protobuf
  static ParserConfig fromProto(
      const envoy::extensions::filters::http::mcp::v3::ParserConfig& proto);
  
  // Default MCP configuration
  static ParserConfig defaultMcpConfig();
};

/**
 * Custom ObjectWriter for field extraction
 */
class FieldExtractorObjectWriter : public google::protobuf::util::converter::ObjectWriter,
                                   public Logger::Loggable<Logger::Id::mcp> {
public:
  FieldExtractorObjectWriter(Protobuf::Struct& metadata, const ParserConfig& config);

  // ObjectWriter interface
  FieldExtractorObjectWriter* StartObject(absl::string_view name) override;
  FieldExtractorObjectWriter* EndObject() override;
  FieldExtractorObjectWriter* StartList(absl::string_view name) override;
  FieldExtractorObjectWriter* EndList() override;
  FieldExtractorObjectWriter* RenderBool(absl::string_view name, bool value) override;
  FieldExtractorObjectWriter* RenderInt32(absl::string_view name, int32_t value) override;
  FieldExtractorObjectWriter* RenderUint32(absl::string_view name, uint32_t value) override;
  FieldExtractorObjectWriter* RenderInt64(absl::string_view name, int64_t value) override;
  FieldExtractorObjectWriter* RenderUint64(absl::string_view name, uint64_t value) override;
  FieldExtractorObjectWriter* RenderDouble(absl::string_view name, double value) override;
  FieldExtractorObjectWriter* RenderFloat(absl::string_view name, float value) override;
  FieldExtractorObjectWriter* RenderString(absl::string_view name, absl::string_view value) override;
  FieldExtractorObjectWriter* RenderBytes(absl::string_view name, absl::string_view value) override;
  FieldExtractorObjectWriter* RenderNull(absl::string_view name) override;

  // Check if we should stop parsing
  bool shouldStop();

private:
  // Check if current path matches any rule and extract if it does
  void checkAndExtract(absl::string_view name, const std::string& value);
  
  // Track path
  void pushPath(absl::string_view name);
  void popPath();
  
  // Check if current path matches a rule
  bool matchesRule(const Rule& rule) const;
  
  Protobuf::Struct& metadata_;
  const ParserConfig& config_;
  
  // Current path in JSON
  std::vector<std::string> path_;
  
  // Track which rules have been matched
  std::vector<bool> matched_rules_;
  
  // Track depth
  int depth_{0};
  
  // Skip arrays
  int array_depth_{0};

  bool all_rules_matched_{false};
};

/**
 * JSON parser with path-based extraction
 */
class JsonPathParser : public Logger::Loggable<Logger::Id::mcp> {
public:
  explicit JsonPathParser(const ParserConfig& config);

  // Parse a chunk of JSON data
  absl::Status parse(absl::string_view data);

  // Finish parsing
  absl::Status finishParse();

  bool allRulesMatched() const { return all_rules_matched_; }

  // Get the extracted metadata
  const Protobuf::Struct& metadata() const { return metadata_; }
  Protobuf::Struct& mutableMetadata() { return metadata_; }

  // Check if this is a valid MCP request (has jsonrpc="2.0" and method)
  bool isValidMcpRequest() const;

  // Reset for reuse
  void reset();

private:
  ParserConfig config_;
  Protobuf::Struct metadata_;
  std::unique_ptr<FieldExtractorObjectWriter> object_writer_;
  std::unique_ptr<google::protobuf::util::converter::JsonStreamParser> stream_parser_;
  bool parsing_started_{false};
  size_t total_bytes_parsed_{0};
  bool all_rules_matched_{false};
};

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
