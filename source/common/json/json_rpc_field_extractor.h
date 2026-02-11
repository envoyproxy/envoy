#pragma once

#include <memory>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

#include "source/common/common/logger.h"
#include "source/common/json/json_rpc_parser_config.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Json {

/**
 * JsonRpcFieldExtractor extracts specific fields from a JSON-RPC stream into Protobuf metadata.
 * It uses the ObjectWriter interface to handle streaming JSON parsing.
 */
class JsonRpcFieldExtractor : public ProtobufUtil::converter::ObjectWriter,
                              public Logger::Loggable<Logger::Id::json_rpc> {
public:
  JsonRpcFieldExtractor(Protobuf::Struct& metadata, const JsonRpcParserConfig& config);
  ~JsonRpcFieldExtractor() override = default;

  // ProtobufUtil::converter::ObjectWriter implementation
  JsonRpcFieldExtractor* StartObject(absl::string_view name) override;
  JsonRpcFieldExtractor* EndObject() override;
  JsonRpcFieldExtractor* StartList(absl::string_view name) override;
  JsonRpcFieldExtractor* EndList() override;
  JsonRpcFieldExtractor* RenderString(absl::string_view name, absl::string_view value) override;
  JsonRpcFieldExtractor* RenderInt32(absl::string_view name, int32_t value) override;
  JsonRpcFieldExtractor* RenderUint32(absl::string_view name, uint32_t value) override;
  JsonRpcFieldExtractor* RenderInt64(absl::string_view name, int64_t value) override;
  JsonRpcFieldExtractor* RenderUint64(absl::string_view name, uint64_t value) override;
  JsonRpcFieldExtractor* RenderDouble(absl::string_view name, double value) override;
  JsonRpcFieldExtractor* RenderFloat(absl::string_view name, float value) override;
  JsonRpcFieldExtractor* RenderBool(absl::string_view name, bool value) override;
  JsonRpcFieldExtractor* RenderNull(absl::string_view name) override;
  JsonRpcFieldExtractor* RenderBytes(absl::string_view name, absl::string_view value) override;

  /**
   * @return true if the extractor has found all required fields and can stop parsing early.
   */
  bool shouldStopParsing() const { return can_stop_parsing_; }

  /**
   * Finalizes extraction by moving data from temporary storage to the final metadata struct.
   */
  void finalizeExtraction();

  // Validation getters
  bool isValidJsonRpc() const { return is_valid_jsonrpc_; }
  const std::string& getMethod() const { return method_; }

protected:
  // Internal state checks for early-stop optimization
  void checkEarlyStop();

  // Stores a field in temporary storage
  void storeField(const std::string& path, const Protobuf::Value& value);

  // Data migration helpers
  void copySelectedFields();
  void copyFieldByPath(const std::string& path);

  // Validation
  void validateRequiredFields();

  // Path helper
  std::string buildFullPath(absl::string_view name) const;

  // Protocol-specific interface
  virtual bool isNotification(const std::string& method) const = 0;
  virtual absl::string_view protocolName() const = 0;
  virtual absl::string_view jsonRpcVersion() const = 0;
  virtual absl::string_view jsonRpcField() const = 0;
  virtual absl::string_view methodField() const = 0;
  virtual bool lists_supported() const = 0;

  Protobuf::Struct temp_storage_;   // Store all fields temporarily
  Protobuf::Struct& root_metadata_; // Final filtered metadata
  const JsonRpcParserConfig& config_;

  // Context stack for building the temporary Protobuf structure
  struct NestedContext {
    Protobuf::Struct* struct_ptr{nullptr};
    Protobuf::ListValue* list_ptr{nullptr};
    std::string field_name{};
    bool is_list() const { return list_ptr != nullptr; }
  };
  std::stack<NestedContext> context_stack_;

  // Current path tracking (e.g., {"params", "user", "id"})
  std::vector<std::string> path_stack_;

  // Field tracking for optimization
  absl::flat_hash_set<std::string> collected_fields_;
  absl::flat_hash_set<std::string> extracted_fields_;

  // Protocol state
  std::string method_;
  bool is_valid_jsonrpc_{false};
  bool has_jsonrpc_{false};
  bool has_method_{false};

  // Optimization flag
  bool can_stop_parsing_{false};

  // Validation state
  std::vector<std::string> missing_required_fields_;

  int depth_{0};
  int array_depth_{0};

  // Cache for path building
  std::string current_path_cache_;
  size_t fields_needed_{0};
  size_t fields_collected_count_{0};
  bool fields_needed_updated_{false};
  bool is_notification_{false};
};

} // namespace Json
} // namespace Envoy
