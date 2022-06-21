#include <string>
#include <vector>

#include "source/common/http/header_map_impl.h"
#include "source/common/json/json_loader.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "contrib/dynamo/filters/http/source/dynamo_request_parser.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Dynamo {
namespace {

TEST(DynamoRequestParser, parseOperation) {
  // Well formed x-amz-target header, in a format, Version.Operation
  {
    Http::TestRequestHeaderMapImpl headers{{"X", "X"}, {"x-amz-target", "X.Operation"}};
    EXPECT_EQ("Operation", RequestParser::parseOperation(headers));
  }

  // Not well formed x-amz-target header.
  {
    Http::TestRequestHeaderMapImpl headers{{"X", "X"}, {"x-amz-target", "X,Operation"}};
    EXPECT_EQ("", RequestParser::parseOperation(headers));
  }

  // Too many entries in the Version.Operation.
  {
    Http::TestRequestHeaderMapImpl headers{{"X", "X"},
                                           {"x-amz-target", "NOT_VALID.NOT_VALID.NOT_VALID"}};
    EXPECT_EQ("", RequestParser::parseOperation(headers));
  }

  // Required header is not present in the headers
  {
    Http::TestRequestHeaderMapImpl headers{{"Z", "Z"}};
    EXPECT_EQ("", RequestParser::parseOperation(headers));
  }
}

TEST(DynamoRequestParser, parseTableNameSingleOperation) {
  std::vector<std::string> supported_single_operations{"GetItem", "Query",      "Scan",
                                                       "PutItem", "UpdateItem", "DeleteItem"};

  {
    std::string json_string = R"EOF(
    {
      "TableName": "Pets",
      "Key": {
        "AnimalType": {"S": "Dog"},
        "Name": {"S": "Fido"}
      }
    }
    )EOF";
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(json_string);

    // Supported operation
    for (const std::string& operation : supported_single_operations) {
      EXPECT_EQ("Pets", RequestParser::parseTable(operation, *json_data).table_name);
    }

    // Not supported operation
    EXPECT_EQ("", RequestParser::parseTable("NotSupportedOperation", *json_data).table_name);
  }

  {
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(R"({"TableName":"Pets"})");
    EXPECT_EQ("Pets", RequestParser::parseTable("GetItem", *json_data).table_name);
  }
}

TEST(DynamoRequestParser, parseTableNameTransactOperation) {
  std::vector<std::string> supported_transact_operations{"TransactGetItems", "TransactWriteItems"};
  // testing single table operation
  {
    std::string json_string = R"EOF(
    {
      "TransactItems": [
        { "Update": { "TableName": "Pets", "Key": { "Name": {"S": "Maxine"} }, "AnimalType": {"S": "Dog"} } },
        { "Put": { "TableName": "Pets", "Key": { "Name": {"S": "Max"} }, "AnimalType": {"S": "Puppy"} } },
        { "Put": { "TableName": "Pets", "Key": { "Name": {"S": "Oscar"} }, "AnimalType": {"S": "Puppy"} } },
        { "Put": { "TableName": "Pets", "Key": { "Name": {"S": "Chloe"} }, "AnimalType": {"S": "Puppy"} } }
      ]
    }
    )EOF";
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(json_string);

    for (const std::string& operation : supported_transact_operations) {
      RequestParser::TableDescriptor table = RequestParser::parseTable(operation, *json_data);
      EXPECT_EQ("Pets", table.table_name);
      EXPECT_TRUE(table.is_single_table);
    }
  }

  // testing multi-table operation
  {
    std::string json_string = R"EOF(
    {
      "TransactItems": [
        { "Put": { "TableName": "Pets", "Key": { "AnimalType": {"S": "Dog"}, "Name": {"S": "Fido"} } } },
        { "Delete": { "TableName": "Strays", "Key": { "AnimalType": {"S": "Dog"}, "Name": {"S": "Fido"} } } },
        { "Put": { "TableName": "Pets", "Key": { "AnimalType": {"S": "Cat"}, "Name": {"S": "Max"} } } },
        { "Delete": { "TableName": "Strays", "Key": { "AnimalType": {"S": "Cat"}, "Name": {"S": "Max"} } } }
      ]
    }
    )EOF";
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(json_string);

    for (const std::string& operation : supported_transact_operations) {
      RequestParser::TableDescriptor table = RequestParser::parseTable(operation, *json_data);
      EXPECT_EQ("", table.table_name);
      EXPECT_FALSE(table.is_single_table);
    }
  }

  // testing missing table
  {
    std::string json_string = R"EOF(
    {
      "TransactItems": [
        { "Put": { "TableName": "" } },
        { "Delete": { "TableName": "Strays", "Key": { "AnimalType": {"S": "Dog"}, "Name": {"S": "Fido"} } } },
        { "Put": { "TableName": "Pets", "Key": { "AnimalType": {"S": "Cat"}, "Name": {"S": "Max"} } } },
        { "Delete": { "TableName": "Strays", "Key": { "AnimalType": {"S": "Cat"}, "Name": {"S": "Max"} } } }
      ]
    }
    )EOF";
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(json_string);

    for (const std::string& operation : supported_transact_operations) {
      RequestParser::TableDescriptor table = RequestParser::parseTable(operation, *json_data);
      EXPECT_EQ("", table.table_name);
      EXPECT_TRUE(table.is_single_table);
    }
  }
}

TEST(DynamoRequestParser, parseErrorType) {
  {
    EXPECT_EQ("ResourceNotFoundException",
              RequestParser::parseErrorType(*Json::Factory::loadFromString(
                  "{\"__type\":\"com.amazonaws.dynamodb.v20120810#ResourceNotFoundException\"}")));
  }

  {
    EXPECT_EQ("ResourceNotFoundException",
              RequestParser::parseErrorType(*Json::Factory::loadFromString(
                  "{\"__type\":\"com.amazonaws.dynamodb.v20120810#ResourceNotFoundException\","
                  "\"message\":\"Requested resource not found: Table: tablename not found\"}")));
  }

  {
    EXPECT_EQ("", RequestParser::parseErrorType(
                      *Json::Factory::loadFromString("{\"__type\":\"UnKnownError\"}")));
  }
}

TEST(DynamoRequestParser, parseTableNameBatchOperation) {
  {
    std::string json_string = R"EOF(
    {
      "RequestItems": {
        "table_1": { "test1" : "something" },
        "table_2": { "test2" : "something" }
      }
    }
    )EOF";
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(json_string);

    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchGetItem", *json_data);
    EXPECT_EQ("", table.table_name);
    EXPECT_FALSE(table.is_single_table);
  }

  {
    std::string json_string = R"EOF(
    {
      "RequestItems": {
        "table_2": { "test1" : "something" },
        "table_2": { "test2" : "something" }
      }
    }
    )EOF";
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(json_string);

    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchGetItem", *json_data);
    EXPECT_EQ("table_2", table.table_name);
    EXPECT_TRUE(table.is_single_table);
  }

  {
    std::string json_string = R"EOF(
    {
      "RequestItems": {
        "table_2": { "test1" : "something" },
        "table_2": { "test2" : "something" },
        "table_3": { "test3" : "something" }
      }
    }
    )EOF";
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(json_string);

    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchGetItem", *json_data);
    EXPECT_EQ("", table.table_name);
    EXPECT_FALSE(table.is_single_table);
  }

  {
    std::string json_string = R"EOF(
    {
      "RequestItems": {
        "table_2": { "test1" : "something" },
        "table_2": { "test2" : "something" }
      }
    }
    )EOF";
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(json_string);

    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchWriteItem", *json_data);
    EXPECT_EQ("table_2", table.table_name);
    EXPECT_TRUE(table.is_single_table);
  }

  {
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString("{}");
    RequestParser::TableDescriptor table =
        RequestParser::parseTable("BatchWriteItem", *Json::Factory::loadFromString("{}"));
    EXPECT_EQ("", table.table_name);
    EXPECT_TRUE(table.is_single_table);
  }

  {
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString("{\"RequestItems\":{}}");
    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchWriteItem", *json_data);
    EXPECT_EQ("", table.table_name);
    EXPECT_TRUE(table.is_single_table);
  }

  {
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString("{}");
    RequestParser::TableDescriptor table = RequestParser::parseTable("BatchGetItem", *json_data);
    EXPECT_EQ("", table.table_name);
    EXPECT_TRUE(table.is_single_table);
  }
}
TEST(DynamoRequestParser, parseBatchUnProcessedKeys) {
  {
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString("{}");
    std::vector<std::string> unprocessed_tables =
        RequestParser::parseBatchUnProcessedKeys(*json_data);
    EXPECT_EQ(0u, unprocessed_tables.size());
  }
  {
    std::vector<std::string> unprocessed_tables = RequestParser::parseBatchUnProcessedKeys(
        *Json::Factory::loadFromString("{\"UnprocessedKeys\":{}}"));
    EXPECT_EQ(0u, unprocessed_tables.size());
  }

  {
    std::vector<std::string> unprocessed_tables = RequestParser::parseBatchUnProcessedKeys(
        *Json::Factory::loadFromString(R"({"UnprocessedKeys":{"table_1" :{}}})"));
    EXPECT_EQ("table_1", unprocessed_tables[0]);
    EXPECT_EQ(1u, unprocessed_tables.size());
  }

  {
    std::string json_string = R"EOF(
    {
      "UnprocessedKeys": {
        "table_1": { "test1" : "something" },
        "table_2": { "test2" : "something" }
      }
    }
    )EOF";
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(json_string);

    std::vector<std::string> unprocessed_tables =
        RequestParser::parseBatchUnProcessedKeys(*json_data);
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
        RequestParser::parsePartitions(*Json::Factory::loadFromString("{}"));
    EXPECT_EQ(0u, partitions.size());
  }
  {
    std::vector<RequestParser::PartitionDescriptor> partitions =
        RequestParser::parsePartitions(*Json::Factory::loadFromString("{\"ConsumedCapacity\":{}}"));
    EXPECT_EQ(0u, partitions.size());
  }
  {
    std::vector<RequestParser::PartitionDescriptor> partitions = RequestParser::parsePartitions(
        *Json::Factory::loadFromString(R"({"ConsumedCapacity":{ "Partitions":{}}})"));
    EXPECT_EQ(0u, partitions.size());
  }
  {
    std::string json_string = R"EOF(
    {
      "ConsumedCapacity": {
        "Partitions": {
          "partition_1" : 0.5,
          "partition_2" : 3.0
        }
      }
    }
    )EOF";
    Json::ObjectSharedPtr json_data = Json::Factory::loadFromString(json_string);

    std::vector<RequestParser::PartitionDescriptor> partitions =
        RequestParser::parsePartitions(*json_data);
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

} // namespace
} // namespace Dynamo
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
