#include "utility.h"

#include "envoy/common/exception.h"

#include "common/json/json_loader.h"

namespace Mongo {

std::string
MessageUtility::collectionFromFullCollectionName(const std::string& full_collection_name) {
  size_t collection_index = full_collection_name.find('.');
  if (collection_index == std::string::npos) {
    throw EnvoyException("invalid full collection name");
  }

  return full_collection_name.substr(collection_index + 1);
}

std::string MessageUtility::queryCommand(const QueryMessage& query) {
  if (query.fullCollectionName().find("$cmd") == std::string::npos) {
    return "";
  }

  // See if there is a $query document, and use that to find the command if so.
  const Bson::Document* doc_to_use = query.query();
  if (query.query()->find("$query", Bson::Field::Type::DOCUMENT)) {
    doc_to_use = &query.query()->find("$query", Bson::Field::Type::DOCUMENT)->asDocument();
  }

  if (doc_to_use->values().empty()) {
    throw EnvoyException("invalid query command");
  }

  return doc_to_use->values().front()->key();
}

std::string MessageUtility::queryCallingFunction(const QueryMessage& query) {
  const Bson::Field* field = query.query()->find("$comment", Bson::Field::Type::STRING);
  if (!field) {
    return "";
  }

  try {
    Json::StringLoader json(field->asString());
    return json.getString("callingFunction");
  } catch (Json::Exception&) {
    return "";
  }
}

MessageUtility::QueryType MessageUtility::queryType(const QueryMessage& query) {
  // First check the top level for _id.
  QueryType type = queryTypeFromDocument(*query.query());
  if (type == QueryType::ScatterGet) {
    // If we didn't find it in the top level, see if we have a top level $query element and look
    // there.
    const Bson::Field* field = query.query()->find("$query", Bson::Field::Type::DOCUMENT);
    if (field) {
      type = queryTypeFromDocument(field->asDocument());
    }
  }

  return type;
}

MessageUtility::QueryType MessageUtility::queryTypeFromDocument(const Bson::Document& document) {
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

} // Mongo
