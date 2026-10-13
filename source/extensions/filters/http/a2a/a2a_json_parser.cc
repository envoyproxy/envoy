#include "source/extensions/filters/http/a2a/a2a_json_parser.h"

#include "source/common/common/assert.h"

#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {

using Json::AttributeExtractionRule;

const std::vector<AttributeExtractionRule>& getMessageSendRules() {
  CONSTRUCT_ON_FIRST_USE(std::vector<AttributeExtractionRule>,
                         {
                             AttributeExtractionRule("params.taskId"),
                             AttributeExtractionRule("params.message.taskId"),
                             AttributeExtractionRule("params.message.contextId"),
                             AttributeExtractionRule("params.message.messageId"),
                             AttributeExtractionRule("params.message.role"),
                             AttributeExtractionRule("params.message.kind"),
                             AttributeExtractionRule("params.message.metadata"),
                             AttributeExtractionRule("params.message.parts"),
                             AttributeExtractionRule("params.configuration"),
                             AttributeExtractionRule("params.metadata"),
                         });
}

const std::vector<AttributeExtractionRule>& getTasksListRules() {
  CONSTRUCT_ON_FIRST_USE(std::vector<AttributeExtractionRule>,
                         {
                             AttributeExtractionRule("params.tenant"),
                             AttributeExtractionRule("params.contextId"),
                             AttributeExtractionRule("params.status"),
                             AttributeExtractionRule("params.pageSize"),
                             AttributeExtractionRule("params.pageToken"),
                             AttributeExtractionRule("params.historyLength"),
                             AttributeExtractionRule("params.lastUpdatedAfter"),
                             AttributeExtractionRule("params.includeArtifacts"),
                         });
}

const std::vector<AttributeExtractionRule>& getTaskPushNotificationRules() {
  CONSTRUCT_ON_FIRST_USE(
      std::vector<AttributeExtractionRule>,
      {
          AttributeExtractionRule("params.taskId"),
          AttributeExtractionRule("params.pushNotificationConfig.id"),
          AttributeExtractionRule("params.pushNotificationConfig.url"),
          AttributeExtractionRule("params.pushNotificationConfig.token"),
          AttributeExtractionRule("params.pushNotificationConfig.authentication"),
      });
}

const std::vector<AttributeExtractionRule>& getTaskIdParamsRules() {
  CONSTRUCT_ON_FIRST_USE(std::vector<AttributeExtractionRule>,
                         {
                             AttributeExtractionRule("params.id"),
                             AttributeExtractionRule("params.metadata"),
                         });
}

const std::vector<AttributeExtractionRule>& getTasksRules() {
  CONSTRUCT_ON_FIRST_USE(std::vector<AttributeExtractionRule>,
                         {
                             AttributeExtractionRule("params.id"),
                             AttributeExtractionRule("params.historyLength"),
                             AttributeExtractionRule("params.metadata"),
                         });
}

const std::vector<AttributeExtractionRule>& getTaskPushNotificationConfigGetRules() {
  CONSTRUCT_ON_FIRST_USE(std::vector<AttributeExtractionRule>,
                         {
                             AttributeExtractionRule("params.id"),
                             AttributeExtractionRule("params.metadata"),
                             AttributeExtractionRule("params.pushNotificationConfigId"),
                         });
}

const std::vector<AttributeExtractionRule>& getTaskPushNotificationConfigDeleteRules() {
  CONSTRUCT_ON_FIRST_USE(std::vector<AttributeExtractionRule>,
                         {
                             AttributeExtractionRule("params.id"),
                             AttributeExtractionRule("params.pushNotificationConfigId"),
                         });
}

void A2aParserConfig::initializeDefaults() {
  // Always extract core JSON-RPC fields
  always_extract_.insert("jsonrpc");
  always_extract_.insert("method");
  // TODO(tyxia) id is required for requests that expect a response. id is NOT present for
  // notifications.
  always_extract_.insert("id");

  addMethodConfig(A2aConstants::Methods::MESSAGE_SEND, getMessageSendRules());
  addMethodConfig(A2aConstants::Methods::MESSAGE_STREAM, getMessageSendRules());

  addMethodConfig(A2aConstants::Methods::TASKS_GET, getTasksRules());

  addMethodConfig(A2aConstants::Methods::TASKS_LIST, getTasksListRules());

  addMethodConfig(A2aConstants::Methods::TASKS_CANCEL, getTaskIdParamsRules());

  addMethodConfig(A2aConstants::Methods::TASKS_PUSH_NOTIFICATION_CONFIG_SET,
                  getTaskPushNotificationRules());

  addMethodConfig(A2aConstants::Methods::TASKS_PUSH_NOTIFICATION_CONFIG_GET,
                  getTaskPushNotificationConfigGetRules());

  addMethodConfig(A2aConstants::Methods::TASKS_RESUBSCRIBE, getTasksRules());

  addMethodConfig(A2aConstants::Methods::TASKS_PUSH_NOTIFICATION_CONFIG_LIST,
                  getTaskPushNotificationConfigGetRules());

  addMethodConfig(A2aConstants::Methods::TASKS_PUSH_NOTIFICATION_CONFIG_DELETE,
                  getTaskPushNotificationConfigDeleteRules());

  addMethodConfig(A2aConstants::Methods::AGENT_GET_AUTHENTICATED_EXTENDED_CARD, {});
}

A2aParserConfig A2aParserConfig::createDefault() {
  A2aParserConfig config;
  config.initializeDefaults();
  return config;
}

A2aFieldExtractor::A2aFieldExtractor(Protobuf::Struct& metadata, const A2aParserConfig& config)
    : JsonRpcFieldExtractor(metadata, config) {}

A2aJsonParser::A2aJsonParser(const A2aParserConfig& config) : config_(config) { reset(); }

absl::Status A2aJsonParser::parse(absl::string_view data) {
  if (!parsing_started_) {
    extractor_ = std::make_unique<A2aFieldExtractor>(metadata_, config_);
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

std::string A2aJsonParser::getMethod() const { return extractor_ ? extractor_->getMethod() : ""; }

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
