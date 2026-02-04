#include "source/extensions/filters/http/a2a/a2a_json_parser.h"

#include "source/common/common/assert.h"

#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {

// Constants and rules are usually defined in the header or a separate constants file.
using Json::AttributeExtractionRule;

void A2aParserConfig::initializeDefaults() {
  // Always extract core JSON-RPC fields
  always_extract_.insert("jsonrpc");
  always_extract_.insert("method");
  always_extract_.insert("id");

  const std::vector<AttributeExtractionRule> message_send_stream_rules = {
      AttributeExtractionRule("params.taskId"),
      AttributeExtractionRule("params.message.taskId"),
      AttributeExtractionRule("params.message.contextId"),
      AttributeExtractionRule("params.message.messageId"),
      AttributeExtractionRule("params.message.role"),
      AttributeExtractionRule("params.message.kind"),
      AttributeExtractionRule("params.message.metadata"),
      AttributeExtractionRule("params.message.parts"),
      AttributeExtractionRule("params.configuration"),
      AttributeExtractionRule("params.metadata")};

  const std::vector<AttributeExtractionRule> task_id_params = {
      AttributeExtractionRule("params.id"), AttributeExtractionRule("params.metadata")};

  addMethodConfig(A2aConstants::Methods::MESSAGE_SEND, message_send_stream_rules);
  addMethodConfig(A2aConstants::Methods::MESSAGE_STREAM, message_send_stream_rules);

  addMethodConfig(A2aConstants::Methods::TASKS_GET,
                  {AttributeExtractionRule("params.id"),
                   AttributeExtractionRule("params.historyLength"),
                   AttributeExtractionRule("params.metadata")});

  addMethodConfig(
      A2aConstants::Methods::TASKS_LIST,
      {AttributeExtractionRule("params.tenant"), AttributeExtractionRule("params.contextId"),
       AttributeExtractionRule("params.status"), AttributeExtractionRule("params.pageSize"),
       AttributeExtractionRule("params.pageToken"), AttributeExtractionRule("params.historyLength"),
       AttributeExtractionRule("params.lastUpdatedAfter"),
       AttributeExtractionRule("params.includeArtifacts")});

  addMethodConfig(A2aConstants::Methods::TASKS_CANCEL, task_id_params);

  addMethodConfig(A2aConstants::Methods::TASKS_PUSH_NOTIFICATION_CONFIG_SET,
                  {AttributeExtractionRule("params.taskId"),
                   AttributeExtractionRule("params.pushNotificationConfig.id"),
                   AttributeExtractionRule("params.pushNotificationConfig.url"),
                   AttributeExtractionRule("params.pushNotificationConfig.token"),
                   AttributeExtractionRule("params.pushNotificationConfig.authentication")});

  addMethodConfig(A2aConstants::Methods::TASKS_PUSH_NOTIFICATION_CONFIG_GET,
                  {AttributeExtractionRule("params.id"), AttributeExtractionRule("params.metadata"),
                   AttributeExtractionRule("params.pushNotificationConfigId")});

  addMethodConfig(A2aConstants::Methods::TASKS_RESUBSCRIBE,
                  {AttributeExtractionRule("params.id"),
                   AttributeExtractionRule("params.historyLength"),
                   AttributeExtractionRule("params.metadata")});

  addMethodConfig(A2aConstants::Methods::TASKS_PUSH_NOTIFICATION_CONFIG_LIST,
                  {AttributeExtractionRule("params.id"), AttributeExtractionRule("params.metadata"),
                   AttributeExtractionRule("params.pushNotificationConfigId")});

  addMethodConfig(A2aConstants::Methods::TASKS_PUSH_NOTIFICATION_CONFIG_DELETE,
                  {AttributeExtractionRule("params.id"),
                   AttributeExtractionRule("params.pushNotificationConfigId")});

  addMethodConfig(A2aConstants::Methods::AGENT_GET_AUTHENTICATED_EXTENDED_CARD, {});
}

A2aParserConfig A2aParserConfig::createDefault() {
  A2aParserConfig config;
  config.initializeDefaults();
  return config;
}

// A2aFieldExtractor implementation
A2aFieldExtractor::A2aFieldExtractor(Protobuf::Struct& metadata, const A2aParserConfig& config)
    : JsonRpcFieldExtractor(metadata, config) {}

// A2aJsonParser implementation
A2aJsonParser::A2aJsonParser(const A2aParserConfig& config) : config_(config) { reset(); }

absl::Status A2aJsonParser::parse(absl::string_view data) {
  if (!parsing_started_) {
    extractor_ = std::make_unique<A2aFieldExtractor>(metadata_, config_);
    // Use the Envoy Protobuf namespace alias
    stream_parser_ = std::make_unique<ProtobufUtil::converter::JsonStreamParser>(extractor_.get());
    parsing_started_ = true;
  }

  auto status = stream_parser_->Parse(data);
  ENVOY_LOG(trace, "A2A JSON parse status: {}", status.ToString());

  if (extractor_->shouldStopParsing()) {
    ENVOY_LOG(trace, "Parser stopped early - all required fields collected");
    all_fields_collected_ = true;
    return finishParse();
  }
  return status;
}

absl::Status A2aJsonParser::finishParse() {
  if (!parsing_started_) {
    return absl::InvalidArgumentError("No data has been parsed");
  }
  ENVOY_LOG(debug, "A2A parser finishing");
  auto status = stream_parser_->FinishParse();
  extractor_->finalizeExtraction();
  return status;
}

const std::string& A2aJsonParser::getMethod() const {
  static const std::string* empty = new std::string("");
  return extractor_ ? extractor_->getMethod() : *empty;
}

const Protobuf::Value* A2aJsonParser::getNestedValue(const std::string& dotted_path) const {
  if (dotted_path.empty()) {
    return nullptr;
  }

  const std::vector<std::string> path = absl::StrSplit(dotted_path, '.');
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

void A2aJsonParser::reset() {
  metadata_.Clear();
  extractor_.reset();
  stream_parser_.reset();
  parsing_started_ = false;
  all_fields_collected_ = false;
}

} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
