#include "common/dynamo/dynamo_request_parser.h"
#include "common/json/json_loader.h"
#include "common/http/header_map_impl.h"

namespace Dynamo {

TEST(DynamoRequestParser, parseOperation) {
  // Well formed x-amz-target header, in a format, Version.Operation
  {
    Http::HeaderMapImpl headers{{"X", "X"}, {"x-amz-target", "X.Operation"}};
    EXPECT_EQ("Operation", RequestParser::parseOperation(headers));
  }

  // Not well formed x-amz-target header.
  {
    Http::HeaderMapImpl headers{{"X", "X"}, {"x-amz-target", "X,Operation"}};
    EXPECT_EQ("", RequestParser::parseOperation(headers));
  }

  // Too many entries in the Version.Operation.
  {
    Http::HeaderMapImpl headers{{"X", "X"}, {"x-amz-target", "NOT_VALID.NOT_VALID.NOT_VALID"}};
    EXPECT_EQ("", RequestParser::parseOperation(headers));
  }

  // Required header is not present in the headers
  {
    Http::HeaderMapImpl headers{{"Z", "Z"}};
    EXPECT_EQ("", RequestParser::parseOperation(headers));
  }
}

TEST(DynamoRequestParser, parseTableNameSingleOperation) {
  std::vector<std::string> supported_single_operations{"GetItem", "Query",      "Scan",
                                                       "PutItem", "UpdateItem", "DeleteItem"};

  {
    std::string json = R"EOF(
    {
      "TableName": "Pets",
      "Key": {
        "AnimalType": {"S": "Dog"},
        "Name": {"S": "Fido"}
      }
    }
    )EOF";

    // Supported operation
    for (const std::string& operation : supported_single_operations) {
      EXPECT_EQ("Pets", RequestParser::parseTable(operation, json).table_name);
    }

    // Not supported operation
    EXPECT_EQ("", RequestParser::parseTable("NotSupportedOperation", json).table_name);
  }

  {
    std::string json = "{\"TableName\":\"Pets\"}";
    EXPECT_EQ("Pets", RequestParser::parseTable("GetItem", json).table_name);
  }

  // Validate invalid json cases
  {
    std::string json = R"EOF(
    {
      "TableName": "Pets",
      "Key": {
        "AnimalType": "S": "Dog"},
        "Name": {"S": "Fido"}
      }
    }
    )EOF";

    EXPECT_THROW(RequestParser::parseTable("GetItem", json), Json::Exception);
  }
}

TEST(DynamoRequestParser, parseErrorType) {
  { EXPECT_THROW(RequestParser::parseErrorType("{test"), Json::Exception); }

  {
    EXPECT_EQ("ResourceNotFoundException",
              RequestParser::parseErrorType(
                  "{\"__type\":\"com.amazonaws.dynamodb.v20120810#ResourceNotFoundException\"}"));
  }

  {
    EXPECT_EQ("ResourceNotFoundException",
              RequestParser::parseErrorType(
                  "{\"__type\":\"com.amazonaws.dynamodb.v20120810#ResourceNotFoundException\","
                  "\"message\":\"Requested resource not found: Table: tablename not found\"}"));
  }

  { EXPECT_EQ("", RequestParser::parseErrorType("{\"__type\":\"UnKnownError\"}")); }

  {
    std::string json = R"EOF(
    {
      "not_important": "test",
      "Key": {
        "AnimalType": "S": "Dog"},
        "Name": {"S": "Fido"}
      }
    }
    )EOF";
    EXPECT_THROW(RequestParser::parseErrorType(json), Json::Exception);
  }
}

TEST(DynamoRequestParser, parseTableNameBatchOperation) {
  {
    std::string json = R"EOF(
    {
      "RequestItems": {
        "table_1": { "test1" : "something" },
        "table_2": { "test2" : "something" }
      }
    }
    )EOF";
    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchGetItem", json);
    EXPECT_EQ("", table.table_name);
    EXPECT_FALSE(table.is_single_table);
  }

  {
    std::string json = R"EOF(
    {
      "RequestItems": {
        "table_2": { "test1" : "something" },
        "table_2": { "test2" : "something" }
      }
    }
    )EOF";
    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchGetItem", json);
    EXPECT_EQ("table_2", table.table_name);
    EXPECT_TRUE(table.is_single_table);
  }

  {
    std::string json = R"EOF(
    {
      "RequestItems": {
        "table_2": { "test1" : "something" },
        "table_2": { "test2" : "something" },
        "table_3": { "test3" : "something" }
      }
    }
    )EOF";
    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchGetItem", json);
    EXPECT_EQ("", table.table_name);
    EXPECT_FALSE(table.is_single_table);
  }

  {
    std::string json = R"EOF(
    {
      "RequestItems": {
        "table_2": { "test1" : "something" },
        "table_2": { "test2" : "something" }
      }
    }
    )EOF";
    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchWriteItem", json);
    EXPECT_EQ("table_2", table.table_name);
    EXPECT_TRUE(table.is_single_table);
  }

  {
    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchWriteItem", "{}");
    EXPECT_EQ("", table.table_name);
    EXPECT_TRUE(table.is_single_table);
  }

  {
    RequestParser::TableDescriptor table =
        RequestParser::parseTable("BatchWriteItem", "{\"RequestItems\":{}}");
    EXPECT_EQ("", table.table_name);
    EXPECT_TRUE(table.is_single_table);
  }

  {
    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchGetItem", "{}");
    EXPECT_EQ("", table.table_name);
    EXPECT_TRUE(table.is_single_table);
  }
}
TEST(DynamoRequestParser, parseBatchUnProcessedKeys) {
  { EXPECT_THROW(RequestParser::parseBatchUnProcessedKeys("{test"), Json::Exception); }

  {
    std::vector<std::string> unprocessed_tables = RequestParser::parseBatchUnProcessedKeys("{}");
    EXPECT_EQ(0u, unprocessed_tables.size());
  }
  {
    std::vector<std::string> unprocessed_tables =
        RequestParser::parseBatchUnProcessedKeys("{\"UnprocessedKeys\":{}}");
    EXPECT_EQ(0u, unprocessed_tables.size());
  }

  {
    std::vector<std::string> unprocessed_tables =
        RequestParser::parseBatchUnProcessedKeys("{\"UnprocessedKeys\":{\"table_1\" :{}}}");
    EXPECT_EQ("table_1", unprocessed_tables[0]);
    EXPECT_EQ(1u, unprocessed_tables.size());
  }

  {
    std::string json = R"EOF(
    {
      "UnprocessedKeys": {
        "table_1": { "test1" : "something" },
        "table_2": { "test2" : "something" }
      }
    }
    )EOF";
    std::vector<std::string> unprocessed_tables = RequestParser::parseBatchUnProcessedKeys(json);
    EXPECT_TRUE(find(unprocessed_tables.begin(), unprocessed_tables.end(), "table_1") !=
                unprocessed_tables.end());
    EXPECT_TRUE(find(unprocessed_tables.begin(), unprocessed_tables.end(), "table_2") !=
                unprocessed_tables.end());
    EXPECT_EQ(2u, unprocessed_tables.size());
  }
}

TEST(DynamoRequestParser, parsePartitionIds) {
  {
    std::vector<RequestParser::PartitionDescriptor> partitions =
        RequestParser::parsePartitions("{}");
    EXPECT_EQ(0u, partitions.size());
  }
  {
    std::vector<RequestParser::PartitionDescriptor> partitions =
        RequestParser::parsePartitions("{\"ConsumedCapacity\":{}}");
    EXPECT_EQ(0u, partitions.size());
  }
  {
    std::vector<RequestParser::PartitionDescriptor> partitions =
        RequestParser::parsePartitions("{\"ConsumedCapacity\":{ \"Partitions\":{}}}");
    EXPECT_EQ(0u, partitions.size());
  }
  {
    std::string json = R"EOF(
    {
      "ConsumedCapacity": {
        "Partitions": {
          "partition_1" : 0.5,
          "partition_2" : 3.0
        }
      }
    }
    )EOF";
    std::vector<RequestParser::PartitionDescriptor> partitions =
        RequestParser::parsePartitions(json);
    for (const RequestParser::PartitionDescriptor& partition : partitions) {
      if (partition.partition_id_ == "partition_1") {
        EXPECT_EQ(1u, partition.capacity_);
      } else {
        EXPECT_EQ(3u, partition.capacity_);
      }
    }
    EXPECT_EQ(2u, partitions.size());
  }
}

} // Dynamo