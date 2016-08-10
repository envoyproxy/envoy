#pragma once

#include "envoy/mongo/codec.h"

namespace Mongo {

/**
 * General utilities for dealing with mongo messages.
 */
class MessageUtility {
public:
  enum class QueryType { PrimaryKey, MultiGet, ScatterGet };

  /**
   * @return the collection name with the database name removed or throw if this cannot be done.
   */
  static std::string collectionFromFullCollectionName(const std::string& full_collection_name);

  /**
   * @return calling function if it can be found in the query. The calling function is found by:
   *         1) Looking for a top level query field name $comment
   *         2) Parsing $comment as a JSON string
   *         3) Accessing the 'callingFunction' field in the JSON.
   *         "" is returned if any of the above fails.
   */
  static std::string queryCallingFunction(const QueryMessage& query);

  /**
   * @return the type of a query message.
   */
  static QueryType queryType(const QueryMessage& query);

  /**
   * @return the name of the command if the query is a command, otherwise "".
   */
  static std::string queryCommand(const QueryMessage& query);

private:
  static QueryType queryTypeFromDocument(const Bson::Document& document);
};

} // Mongo
