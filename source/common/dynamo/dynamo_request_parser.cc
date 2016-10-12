#include "dynamo_request_parser.h"

#include "common/common/utility.h"
#include "common/json/json_loader.h"

namespace Dynamo {

/*
 * Basic json request/response format:
 * http://docs.aws.amazon.com/amazondynamodb/latest/developerguide/Appendix.CurrentAPI.html
 */
const Http::LowerCaseString RequestParser::X_AMZ_TARGET("X-AMZ-TARGET");

// clang-format off

const std::vector<std::string> RequestParser::SINGLE_TABLE_OPERATIONS{
    "CreateTable",
    "DeleteItem",
    "DeleteTable",
    "DescribeTable",
    "GetItem",
    "PutItem",
    "Query",
    "Scan",
    "UpdateItem",
    "UpdateTable"};

const std::vector<std::string> RequestParser::SUPPORTED_ERROR_TYPES{
    // 4xx
    "AccessDeniedException",
    "ConditionalCheckFailedException",
    "IncompleteSignatureException",
    "ItemCollectionSizeLimitExceededException",
    "LimitExceededException",
    "MissingAuthenticationTokenException",
    "ProvisionedThroughputExceededException",
    "ResourceInUseException",
    "ResourceNotFoundException",
    "ThrottlingException",
    "UnrecognizedClientException",
    "ValidationException"};

// clang-format on

const std::vector<std::string> RequestParser::BATCH_OPERATIONS{"BatchGetItem", "BatchWriteItem"};

std::string RequestParser::parseOperation(const Http::HeaderMap& headerMap) {
  std::string operation;

  const std::string& x_amz_target = headerMap.get(X_AMZ_TARGET);
  if (!x_amz_target.empty()) {
    // Normally x-amz-target contains Version.Operation, e.g., DynamoDB_20160101.GetItem
    std::vector<std::string> version_and_operation = StringUtil::split(x_amz_target, '.');
    if (version_and_operation.size() == 2) {
      operation = version_and_operation[1];
    }
  }

  return operation;
}

RequestParser::TableDescriptor RequestParser::parseTable(const std::string& operation,
                                                         const std::string& data) {
  TableDescriptor table{"", true};

  // Simple operations on a single table, have "TableName" explicitly specified.
  if (find(SINGLE_TABLE_OPERATIONS.begin(), SINGLE_TABLE_OPERATIONS.end(), operation) !=
      SINGLE_TABLE_OPERATIONS.end()) {
    Json::StringLoader json(data);
    if (json.hasObject("TableName")) {
      table.table_name = json.getString("TableName");
    }
  } else if (find(BATCH_OPERATIONS.begin(), BATCH_OPERATIONS.end(), operation) !=
             BATCH_OPERATIONS.end()) {
    Json::StringLoader json(data);
    if (json.hasObject("RequestItems")) {
      Json::Object tables = json.getObject("RequestItems");
      tables.iterate([&table](const std::string& key, const Json::Object&) {
        if (table.table_name.empty()) {
          table.table_name = key;
        } else {
          if (table.table_name != key) {
            table.table_name = "";
            table.is_single_table = false;
            return false;
          }
        }

        return true;
      });
    }
  }

  return table;
}
std::vector<std::string> RequestParser::parseBatchUnProcessedKeys(const std::string& data) {
  std::vector<std::string> unprocessed_tables;
  Json::StringLoader json(data);
  if (json.hasObject("UnprocessedKeys")) {
    Json::Object tables = json.getObject("UnprocessedKeys");
    tables.iterate([&unprocessed_tables](const std::string& key, const Json::Object&) {
      unprocessed_tables.emplace_back(key);
      return true;
    });
  }
  return unprocessed_tables;
}
std::string RequestParser::parseErrorType(const std::string& data) {
  Json::StringLoader json(data);

  if (json.hasObject("__type")) {
    std::string error_type = json.getString("__type");
    for (const std::string& supported_error_type : SUPPORTED_ERROR_TYPES) {
      if (StringUtil::endsWith(error_type, supported_error_type)) {
        return supported_error_type;
      }
    }
  }

  return "";
}

bool RequestParser::isBatchOperation(const std::string& operation) {
  return find(BATCH_OPERATIONS.begin(), BATCH_OPERATIONS.end(), operation) !=
         BATCH_OPERATIONS.end();
}

std::vector<RequestParser::PartitionDescriptor>
RequestParser::parsePartitionIds(const std::string& data) {
  Json::StringLoader json(data);
  std::vector<RequestParser::PartitionDescriptor> partition_descriptors;

  if (json.hasObject("ConsumedCapacity")) {
    Json::Object consumed_capacity = json.getObject("ConsumedCapacity");
    if (consumed_capacity.hasObject("Partitions")) {
      Json::Object partitions = consumed_capacity.getObject("Partitions");

      partitions.iterate(
          [&partition_descriptors, &partitions](const std::string& key, const Json::Object&) {
            // Capacity is a double and it is rounded up to the nearest integer.
            // The stats for partition is incremented based on the capacity. 
            // Ex if Capacity == 2, the stats for key will be incremented by 2.
            uint64_t capacity_integer =
                static_cast<uint64_t>(std::ceil(partitions.getDouble(key, 0.0)));
            // Dynamo::RequestParser::PartitionDescriptor partition = {key, capacity_integer};
            partition_descriptors.emplace_back(key, capacity_integer);
            return true;
          });
    }
  }
  return partition_descriptors;
}

} // Dynamo