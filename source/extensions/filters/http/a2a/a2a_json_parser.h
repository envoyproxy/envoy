#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "envoy/common/pure.h"

#include "source/common/common/logger.h"
#include "source/common/json/json_rpc_field_extractor.h"
#include "source/common/json/json_rpc_parser_config.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {

/**
 * A2A protocol constants
 */
namespace A2aConstants {
// JSON-RPC constants
constexpr absl::string_view JSONRPC_VERSION = "2.0";
constexpr absl::string_view JSONRPC_FIELD = "jsonrpc";
constexpr absl::string_view METHOD_FIELD = "method";
constexpr absl::string_view ID_FIELD = "id";

// Method names
namespace Methods {
constexpr absl::string_view MESSAGE_SEND = "message/send";
constexpr absl::string_view MESSAGE_STREAM = "message/stream";
constexpr absl::string_view TASKS_GET = "tasks/get";
constexpr absl::string_view TASKS_LIST = "tasks/list";
constexpr absl::string_view TASKS_CANCEL = "tasks/cancel";
constexpr absl::string_view TASKS_PUSH_NOTIFICATION_CONFIG_SET = "tasks/pushNotificationConfig/set";
constexpr absl::string_view TASKS_PUSH_NOTIFICATION_CONFIG_GET = "tasks/pushNotificationConfig/get";
constexpr absl::string_view TASKS_RESUBSCRIBE = "tasks/resubscribe";
constexpr absl::string_view TASKS_PUSH_NOTIFICATION_CONFIG_LIST =
    "tasks/pushNotificationConfig/list";
constexpr absl::string_view TASKS_PUSH_NOTIFICATION_CONFIG_DELETE =
    "tasks/pushNotificationConfig/delete";
constexpr absl::string_view AGENT_GET_AUTHENTICATED_EXTENDED_CARD =
    "agent/getAuthenticatedExtendedCard";
} // namespace Methods
} // namespace A2aConstants

/**
 * Configuration for A2A field extraction
 */
class A2aParserConfig : public Json::JsonRpcParserConfig {
public:
  // Create default config (minimal extraction)
  static A2aParserConfig createDefault();

protected:
  void initializeDefaults() override;
};

/**
 * A2A JSON field extractor with early stopping optimization
 */
class A2aFieldExtractor : public Json::JsonRpcFieldExtractor {
public:
  A2aFieldExtractor(Protobuf::Struct& metadata, const A2aParserConfig& config);

protected:
  bool lists_supported() const override { return true; }
  bool isNotification(const std::string& /*method*/) const override {
    // A2A does not have notifications in JSON-RPC sense.
    return false;
  }
  absl::string_view protocolName() const override { return "A2A"; }
  absl::string_view jsonRpcVersion() const override { return A2aConstants::JSONRPC_VERSION; }
  absl::string_view jsonRpcField() const override { return A2aConstants::JSONRPC_FIELD; }
  absl::string_view methodField() const override { return A2aConstants::METHOD_FIELD; }
};

/**
 * A2A JSON parser with selective field extraction
 */
class A2aJsonParser : public Logger::Loggable<Logger::Id::a2a> {
public:
  // Constructor with optional config (defaults to minimal extraction)
  explicit A2aJsonParser(const A2aParserConfig& config = A2aParserConfig::createDefault());

  // Parse a chunk of JSON data
  absl::Status parse(absl::string_view data);

  // Finish parsing
  absl::Status finishParse();

  // Check if this is a valid A2A request
  bool isValidA2aRequest() const { return extractor_ && extractor_->isValidJsonRpc(); }

  bool isAllFieldsCollected() const { return all_fields_collected_; }

  // Get the method string
  std::string getMethod() const;

  // Get the extracted metadata (only contains configured fields)
  const Protobuf::Struct& metadata() const { return metadata_; }

  // Helper to get nested value from metadata
  const Protobuf::Value* getNestedValue(const std::string& dotted_path) const;

  // Reset parser
  void reset();

private:
  // TODO(tyxia): This config_ could be stored as a reference to if needed?
  A2aParserConfig config_;
  Protobuf::Struct metadata_;
  std::unique_ptr<A2aFieldExtractor> extractor_;
  std::unique_ptr<ProtobufUtil::converter::JsonStreamParser> stream_parser_;
  bool parsing_started_{false};
  bool all_fields_collected_{false};
};

} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
