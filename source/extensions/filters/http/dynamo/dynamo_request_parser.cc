#include "extensions/filters/http/dynamo/dynamo_request_parser.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "common/common/utility.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {

/**
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
    "IdempotentParameterMismatchException",
    "IncompleteSignatureException",
    "ItemCollectionSizeLimitExceededException",
    "LimitExceededException",
    "MissingAuthenticationTokenException",
    "ProvisionedThroughputExceededException",
    "ResourceInUseException",
    "ResourceNotFoundException",
    "ThrottlingException",
    "TransactionCanceledException",
    "TransactionInProgressException",
    "UnrecognizedClientException",
    "ValidationException"};
// clang-format on

const std::vector<std::string> RequestParser::BATCH_OPERATIONS{"BatchGetItem", "BatchWriteItem"};

const std::vector<std::string> RequestParser::TRANSACT_OPERATIONS{"TransactGetItems",
                                                                  "TransactWriteItems"};
const std::vector<std::string> RequestParser::TRANSACT_ITEM_OPERATIONS{"ConditionCheck", "Delete",
                                                                       "Get", "Put", "Update"};

std::string RequestParser::parseOperation(const Http::HeaderMap& header_map) {
  std::string operation;

  const Http::HeaderEntry* x_amz_target = header_map.get(X_AMZ_TARGET);
  if (x_amz_target) {
    // Normally x-amz-target contains Version.Operation, e.g., DynamoDB_20160101.GetItem
    auto version_and_operation = StringUtil::splitToken(x_amz_target->value().getStringView(), ".");
    if (version_and_operation.size() == 2) {
      operation = std::string{version_and_operation[1]};
    }
  }

  return operation;
}

RequestParser::TableDescriptor RequestParser::parseTable(const std::string& operation,
                                                         const Json::Object& json_data) {
  TableDescriptor table{"", true};

  // Simple operations on a single table, have "TableName" explicitly specified.
  if (find(SINGLE_TABLE_OPERATIONS.begin(), SINGLE_TABLE_OPERATIONS.end(), operation) !=
      SINGLE_TABLE_OPERATIONS.end()) {
    table.table_name = json_data.getString("TableName", "");
  } else if (find(BATCH_OPERATIONS.begin(), BATCH_OPERATIONS.end(), operation) !=
             BATCH_OPERATIONS.end()) {
    Json::ObjectSharedPtr tables = json_data.getObject("RequestItems", true);
    tables->iterate([&table](const std::string& key, const Json::Object&) {
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
  } else if (find(TRANSACT_OPERATIONS.begin(), TRANSACT_OPERATIONS.end(), operation) !=
             TRANSACT_OPERATIONS.end()) {
    std::vector<Json::ObjectSharedPtr> transact_items =
        json_data.getObjectArray("TransactItems", true);
    for (const Json::ObjectSharedPtr& transact_item : transact_items) {
      const auto next_table_name = getTableNameFromTransactItem(*transact_item);
      if (!next_table_name.has_value()) {
        // if an operation is missing a table name, we want to throw the normal set of errors
        table.table_name = "";
        table.is_single_table = true;
        break;
      }
      if (table.table_name.empty()) {
        table.table_name = next_table_name.value();
      } else if (table.table_name != next_table_name.value()) {
        table.table_name = "";
        table.is_single_table = false;
        break;
      }
    }
  }
  return table;
}

absl::optional<std::string>
RequestParser::getTableNameFromTransactItem(const Json::Object& transact_item) {
  for (const std::string& operation : TRANSACT_ITEM_OPERATIONS) {
    Json::ObjectSharedPtr item = transact_item.getObject(operation, true);
    std::string table_name = item->getString("TableName", "");
    if (!table_name.empty()) {
      return absl::make_optional(table_name);
    }
  }
  return absl::nullopt;
}

std::vector<std::string> RequestParser::parseBatchUnProcessedKeys(const Json::Object& json_data) {
  std::vector<std::string> unprocessed_tables;
  Json::ObjectSharedPtr tables = json_data.getObject("UnprocessedKeys", true);
  tables->iterate([&unprocessed_tables](const std::string& key, const Json::Object&) {
    unprocessed_tables.emplace_back(key);
    return true;
  });

  return unprocessed_tables;
}

std::string RequestParser::parseErrorType(const Json::Object& json_data) {
  std::string error_type = json_data.getString("__type", "");
  if (error_type.empty()) {
    return "";
  }

  for (const std::string& supported_error_type : SUPPORTED_ERROR_TYPES) {
    if (absl::EndsWith(error_type, supported_error_type)) {
      return supported_error_type;
    }
  }

  return "";
}

bool RequestParser::isBatchOperation(const std::string& operation) {
  return find(BATCH_OPERATIONS.begin(), BATCH_OPERATIONS.end(), operation) !=
         BATCH_OPERATIONS.end();
}

std::vector<RequestParser::PartitionDescriptor>
RequestParser::parsePartitions(const Json::Object& json_data) {
  std::vector<RequestParser::PartitionDescriptor> partition_descriptors;

  Json::ObjectSharedPtr partitions =
      json_data.getObject("ConsumedCapacity", true)->getObject("Partitions", true);
  partitions->iterate([&partition_descriptors, &partitions](const std::string& key,
                                                            const Json::Object&) {
    // For a given partition id, the amount of capacity used is returned in the body as a double.
    // A stat will be created to track the capacity consumed for the operation, table and partition.
    // Stats counter only increments by whole numbers, capacity is round up to the nearest integer
    // to account for this.
    uint64_t capacity_integer = static_cast<uint64_t>(std::ceil(partitions->getDouble(key, 0.0)));
    partition_descriptors.emplace_back(key, capacity_integer);
    return true;
  });

  return partition_descriptors;
}

void RequestParser::forEachStatString(const StringFn& fn) {
  for (const std::string& str : SINGLE_TABLE_OPERATIONS) {
    fn(str);
  }
  for (const std::string& str : SUPPORTED_ERROR_TYPES) {
    fn(str);
  }
  for (const std::string& str : BATCH_OPERATIONS) {
    fn(str);
  }
  for (const std::string& str : TRANSACT_OPERATIONS) {
    fn(str);
  }
  for (const std::string& str : TRANSACT_ITEM_OPERATIONS) {
    fn(str);
  }
}

} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
