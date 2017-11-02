#include "common/mongo/utility.h"

#include <string>

#include "envoy/common/exception.h"

#include "common/json/json_loader.h"

namespace Envoy {
namespace Mongo {

QueryMessageInfo::QueryMessageInfo(const QueryMessage& query)
    : request_id_{query.requestId()}, max_time_{0} {
  // First see if this is a command, if so we are done.
  const Bson::Document* command = parseCommand(query);
  if (command) {
    command_ = command->values().front()->key();

    // Special case the 3.2 'find' command since it is a query.
    if (command_ == "find") {
      command_ = "";
      parseFindCommand(*command);
    }

    return;
  }

  // Standard query.
  collection_ = parseCollection(query.fullCollectionName());
  callsite_ = parseCallingFunction(query);
  max_time_ = parseMaxTime(query);
  type_ = parseType(query);
}

std::string QueryMessageInfo::parseCollection(const std::string& full_collection_name) {
  size_t collection_index = full_collection_name.find('.');
  if (collection_index == std::string::npos) {
    throw EnvoyException("invalid full collection name");
  }

  return full_collection_name.substr(collection_index + 1);
}

int32_t QueryMessageInfo::parseMaxTime(const QueryMessage& query) {
  const Bson::Field* field = query.query()->find("$maxTimeMS");
  if (!field) {
    return 0;
  }

  if (field->type() == Bson::Field::Type::INT32) {
    return field->asInt32();
  } else if (field->type() == Bson::Field::Type::INT64) {
    return static_cast<int32_t>(field->asInt64());
  } else {
    return 0;
  }
}

const Bson::Document* QueryMessageInfo::parseCommand(const QueryMessage& query) {
  if (query.fullCollectionName().find("$cmd") == std::string::npos) {
    return nullptr;
  }

  // See if there is a $query document, and use that to find the command if so.
  const Bson::Document* doc_to_use = query.query();
  const Bson::Field* field = query.query()->find("$query", Bson::Field::Type::DOCUMENT);
  if (field) {
    doc_to_use = &field->asDocument();
  }

  if (doc_to_use->values().empty()) {
    throw EnvoyException("invalid query command");
  }

  return doc_to_use;
}

std::string QueryMessageInfo::parseCallingFunction(const QueryMessage& query) {
  const Bson::Field* field = query.query()->find("$comment", Bson::Field::Type::STRING);
  if (!field) {
    return "";
  }

  return parseCallingFunctionJson(field->asString());
}

std::string QueryMessageInfo::parseCallingFunctionJson(const std::string& json_string) {
  try {
    Json::ObjectSharedPtr json = Json::Factory::loadFromString(json_string);
    return json->getString("callingFunction");
  } catch (Json::Exception&) {
    return "";
  }
}

QueryMessageInfo::QueryType QueryMessageInfo::parseType(const QueryMessage& query) {
  // First check the top level for _id.
  QueryType type = parseTypeFromDocument(*query.query());
  if (type == QueryType::ScatterGet) {
    // If we didn't find it in the top level, see if we have a top level $query element and look
    // there.
    const Bson::Field* field = query.query()->find("$query", Bson::Field::Type::DOCUMENT);
    if (field) {
      type = parseTypeFromDocument(field->asDocument());
    }
  }

  return type;
}

QueryMessageInfo::QueryType
QueryMessageInfo::parseTypeFromDocument(const Bson::Document& document) {
  const Bson::Field* field = document.find("_id");
  if (!field) {
    return QueryType::ScatterGet;
  }

  // For now we call any query where _id is equal to a non-scalar value a multi get.
  if (field->type() == Bson::Field::Type::DOCUMENT || field->type() == Bson::Field::Type::ARRAY) {
    return QueryType::MultiGet;
  }

  return QueryType::PrimaryKey;
}

void QueryMessageInfo::parseFindCommand(const Bson::Document& command) {
  collection_ = command.values().front()->asString();
  const Bson::Field* comment = command.find("comment", Bson::Field::Type::STRING);
  if (comment) {
    callsite_ = parseCallingFunctionJson(comment->asString());
  }

  const Bson::Field* filter = command.find("filter", Bson::Field::Type::DOCUMENT);
  if (filter) {
    type_ = parseTypeFromDocument(filter->asDocument());
  }
}

} // namespace Mongo
} // namespace Envoy
