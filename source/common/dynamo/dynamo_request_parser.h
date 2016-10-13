#pragma once

#include "envoy/http/header_map.h"

namespace Dynamo {

/*
 * Request parser for dynamodb request/response.
 *
 * Basic dynamodb json request/response format:
 * http://docs.aws.amazon.com/amazondynamodb/latest/developerguide/Appendix.CurrentAPI.html
 */
class RequestParser {
public:
  struct TableDescriptor {
  public:
    std::string table_name;
    bool is_single_table;
  };

  struct PartitionDescriptor {
    PartitionDescriptor(std::string partition, uint64_t capacity)
        : partition_id_(partition), capacity_(capacity) {}
    std::string partition_id_;
    // The capacity returned with a partition id is a real number. We round up the capacity.
    uint64_t capacity_;
  };

  /**
   * Parse operation out of x-amz-target header.
   * @return empty string if operation cannot be parsed.
   */
  static std::string parseOperation(const Http::HeaderMap& headerMap);

  /**
   * Parse table name out of data, based on the operation.
   * @return empty string as TableDescriptor.table_name if table name cannot be parsed out of valid
   *json data
   * or if operation is not in the list of operations that we support.
   *
   * For simple operations on single table, e.g., GetItem, PutItem, Query etc @return table
   * name in TableDescriptor.table_name.
   *
   * For batch operations, e.g. BatchGetItem/BatchWriteItem, @return table name in
   *TableDescriptor.table_name if it's only one
   * table used in all operations, @return empty string in TableDescriptor.table_name and
   *TableDescriptor.is_single_table=false in case of multiple.
   *
   * @throw Json::Exception if data is not in valid Json format.
   */
  static TableDescriptor parseTable(const std::string& operation, const std::string& data);

  /**
   * Parse error details which might be provided for a given response code.
   * @return empty string if cannot get error details.
   * For the full list of errors, see
   * http://docs.aws.amazon.com/amazondynamodb/latest/APIReference/CommonErrors.html
   * Operation specific errors, for example, error section of
   * http://docs.aws.amazon.com/amazondynamodb/latest/APIReference/API_UpdateItem.html
   *
   * @throw Json::Exception if data is not in valid Json format.
   */
  static std::string parseErrorType(const std::string& data);

  /**
   * Parse unprocessed keys for batch operation results.
   * @return empty set if there are no unprocessed keys or a set of table names that did not get
   * processed in the batch operation.
   */
  static std::vector<std::string> parseBatchUnProcessedKeys(const std::string& data);

  /**
   * @return true if the operation is in the set of supported BATCH_OPERATIONS
   */
  static bool isBatchOperation(const std::string& operation);

  /**
   * @return empty set if there are no partition ids or a set of partition ids
   */
  static std::vector<PartitionDescriptor> parsePartitions(const std::string& data);

private:
  static const Http::LowerCaseString X_AMZ_TARGET;
  static const std::vector<std::string> SINGLE_TABLE_OPERATIONS;
  static const std::vector<std::string> BATCH_OPERATIONS;

  // http://docs.aws.amazon.com/amazondynamodb/latest/developerguide/Programming.Errors.html
  static const std::vector<std::string> SUPPORTED_ERROR_TYPES;

  RequestParser() {}
};

} // Dynamo