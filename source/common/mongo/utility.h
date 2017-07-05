#pragma once

#include <string>

#include "envoy/mongo/codec.h"

namespace Envoy {
namespace Mongo {

/**
 * Parses a query into information that can be used for stat gathering.
 */
class QueryMessageInfo {
public:
  enum class QueryType { PrimaryKey, MultiGet, ScatterGet };

  QueryMessageInfo(const QueryMessage& query);

  /**
   * @return the query's request ID.
   */
  int32_t requestId() { return request_id_; }

  /**
   * @return the collection name with the database name removed, or "" if a command.
   */
  const std::string& collection() { return collection_; }

  /**
   * @return calling function if it can be found in the query. The calling function is found by:
   *         1) Looking for a top level query field name $comment
   *         2) Parsing $comment as a JSON string
   *         3) Accessing the 'callingFunction' field in the JSON.
   *         "" is returned if any of the above fails.
   */
  const std::string& callsite() { return callsite_; }

  /**
   * @return the value of maxTimeMS or 0 if not given.
   */
  int32_t max_time() { return max_time_; }

  /**
   * @return the type of a query message.
   */
  QueryType type() { return type_; }

  /**
   * @return the name of the command if the query is a command, otherwise "".
   */
  const std::string& command() { return command_; }

private:
  std::string parseCallingFunction(const QueryMessage& query);
  std::string parseCallingFunctionJson(const std::string& json_string);
  std::string parseCollection(const std::string& full_collection_name);
  int32_t parseMaxTime(const QueryMessage& query);
  const Bson::Document* parseCommand(const QueryMessage& query);
  void parseFindCommand(const Bson::Document& command);
  QueryType parseType(const QueryMessage& query);
  QueryType parseTypeFromDocument(const Bson::Document& document);

  int32_t request_id_;
  std::string collection_;
  std::string callsite_;
  int32_t max_time_;
  QueryType type_{QueryType::ScatterGet};
  std::string command_;
};

} // namespace Mongo
} // namespace Envoy
