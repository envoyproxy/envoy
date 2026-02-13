#include "source/extensions/filters/http/a2a/a2a_json_parser.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {
namespace {

class A2aJsonParserTest : public ::testing::Test {
protected:
  A2aJsonParserTest() : parser_(A2aParserConfig::createDefault()) {}

  A2aJsonParser parser_;
};

// TODO(tyxia) Handle and test top-level ID field.
TEST_F(A2aJsonParserTest, ParseSimpleMessageSend) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "message/send",
    "params": {
      "taskId": "task-abc-987",
      "message": {
        "taskId": "task1",
        "contextId": "context1",
        "messageId": "msg1",
        "role": "user",
      },
      "configuration": {
        "blocking": true,
        "acceptedOutputModes": ["text/plain"]
      },
      "metadata": {
        "baz": "qux"
      }
    }
  })";

  // Parse the JSON string.
  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());

  // Verify overall validity and method.
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "message/send");

  // Verify top-level extracted fields.
  EXPECT_EQ(parser_.metadata().fields().at("method").string_value(), "message/send");

  // Verify fields within params.
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("taskId").string_value(),
      "task-abc-987");

  // Verify fields within params.message.
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("taskId")
                .string_value(),
            "task1");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("contextId")
                .string_value(),
            "context1");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("messageId")
                .string_value(),
            "msg1");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("role")
                .string_value(),
            "user");

  // Verify fields within params.configuration.
  EXPECT_TRUE(parser_.metadata()
                  .fields()
                  .at("params")
                  .struct_value()
                  .fields()
                  .at("configuration")
                  .struct_value()
                  .fields()
                  .at("blocking")
                  .bool_value());

  // Verify fields within params.metadata.
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("metadata")
                .struct_value()
                .fields()
                .at("baz")
                .string_value(),
            "qux");
  EXPECT_TRUE(parser_.metadata()
                  .fields()
                  .at("params")
                  .struct_value()
                  .fields()
                  .at("configuration")
                  .struct_value()
                  .fields()
                  .at("acceptedOutputModes")
                  .has_list_value());
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("configuration")
                .struct_value()
                .fields()
                .at("acceptedOutputModes")
                .list_value()
                .values_size(),
            1);
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("configuration")
                .struct_value()
                .fields()
                .at("acceptedOutputModes")
                .list_value()
                .values(0)
                .string_value(),
            "text/plain");
}

TEST_F(A2aJsonParserTest, ParseMessageSend) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "message/send",
    "id": "123",
    "params": {
      "taskId": "task-abc-987",
      "message": {
        "taskId": "task1",
        "contextId": "context1",
        "messageId": "msg1",
        "role": "user",
        "parts": [
          {
            "type": "text",
            "text": "Can you analyze the attached CSV for Q3 sales trends?"
          },
          {
            "type": "file",
            "file": {
              "mimeType": "text/csv",
              "uri": "https://example.com/secure/data.csv"
            }
          }
        ],
        "kind": "message",
        "metadata": {"foo": "bar"}
      },
      "configuration": {
        "blocking": true,
        "acceptedOutputModes": ["text/plain"]
      },
      "metadata": {
        "baz": "qux"
      }
    }
  })";

  // Parse the JSON string.
  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());

  // Verify overall validity and method.
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "message/send");

  // Verify top-level extracted fields.
  EXPECT_EQ(parser_.metadata().fields().at("id").string_value(), "123");
  EXPECT_EQ(parser_.metadata().fields().at("method").string_value(), "message/send");

  // Verify fields within params.
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("taskId").string_value(),
      "task-abc-987");

  // Verify fields within params.message.
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("taskId")
                .string_value(),
            "task1");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("contextId")
                .string_value(),
            "context1");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("messageId")
                .string_value(),
            "msg1");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("role")
                .string_value(),
            "user");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("kind")
                .string_value(),
            "message");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("metadata")
                .struct_value()
                .fields()
                .at("foo")
                .string_value(),
            "bar");

  // Verify list within params.message.parts.
  EXPECT_TRUE(parser_.metadata()
                  .fields()
                  .at("params")
                  .struct_value()
                  .fields()
                  .at("message")
                  .struct_value()
                  .fields()
                  .at("parts")
                  .has_list_value());
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("parts")
                .list_value()
                .values_size(),
            2);
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("parts")
                .list_value()
                .values(0)
                .struct_value()
                .fields()
                .at("text")
                .string_value(),
            "Can you analyze the attached CSV for Q3 sales trends?");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("message")
                .struct_value()
                .fields()
                .at("parts")
                .list_value()
                .values(1)
                .struct_value()
                .fields()
                .at("file")
                .struct_value()
                .fields()
                .at("uri")
                .string_value(),
            "https://example.com/secure/data.csv");

  // Verify fields within params.configuration.
  EXPECT_TRUE(parser_.metadata()
                  .fields()
                  .at("params")
                  .struct_value()
                  .fields()
                  .at("configuration")
                  .struct_value()
                  .fields()
                  .at("blocking")
                  .bool_value());

  // Verify fields within params.metadata.
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("metadata")
                .struct_value()
                .fields()
                .at("baz")
                .string_value(),
            "qux");
  EXPECT_TRUE(parser_.metadata()
                  .fields()
                  .at("params")
                  .struct_value()
                  .fields()
                  .at("configuration")
                  .struct_value()
                  .fields()
                  .at("acceptedOutputModes")
                  .has_list_value());
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("configuration")
                .struct_value()
                .fields()
                .at("acceptedOutputModes")
                .list_value()
                .values_size(),
            1);
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("configuration")
                .struct_value()
                .fields()
                .at("acceptedOutputModes")
                .list_value()
                .values(0)
                .string_value(),
            "text/plain");
}

TEST_F(A2aJsonParserTest, ParseMessageSendMultiChunks) {
  const std::string part1 = R"({
    "jsonrpc": "2.0",
    "method": "message/send",
    "id": "123",
    "params": {
      "taskId": "task-abc-987",
      "message": {
        "taskId": "task1")";

  const std::string part2 = R"(,
        "contextId": "context1",
        "messageId": "msg1",
        "role": "user",
        "parts": [
          {
            "type": "text",
            "text": "Can you analyze the attached CSV for Q3 sales trends?"
          }
        ]
      }
    }
  })";

  ASSERT_TRUE(parser_.parse(part1).ok());
  ASSERT_TRUE(parser_.parse(part2).ok());
  ASSERT_TRUE(parser_.finishParse().ok());

  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "message/send");
  EXPECT_EQ(parser_.metadata().fields().at("jsonrpc").string_value(), "2.0");
  EXPECT_EQ(parser_.metadata().fields().at("method").string_value(), "message/send");
  EXPECT_EQ(parser_.metadata().fields().at("id").string_value(), "123");

  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("taskId").string_value(),
      "task-abc-987");

  const auto& message =
      parser_.metadata().fields().at("params").struct_value().fields().at("message").struct_value();
  EXPECT_EQ(message.fields().at("taskId").string_value(), "task1");
  EXPECT_EQ(message.fields().at("contextId").string_value(), "context1");
  EXPECT_EQ(message.fields().at("messageId").string_value(), "msg1");
  EXPECT_EQ(message.fields().at("role").string_value(), "user");

  EXPECT_TRUE(message.fields().at("parts").has_list_value());
  EXPECT_EQ(message.fields().at("parts").list_value().values_size(), 1);
  const auto& part0 = message.fields().at("parts").list_value().values(0).struct_value();
  EXPECT_EQ(part0.fields().at("type").string_value(), "text");
  EXPECT_EQ(part0.fields().at("text").string_value(),
            "Can you analyze the attached CSV for Q3 sales trends?");
}

TEST_F(A2aJsonParserTest, ParseTasksGet) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tasks/get",
    "id": "124",
    "params": {
      "id": "task1",
      "historyLength": 10,
      "metadata": {
        "foo": "bar"
      }
    }
  })";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/get");
  EXPECT_EQ(parser_.metadata().fields().at("id").string_value(), "124");

  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("id").string_value(),
      "task1");

  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("historyLength")
                .number_value(),
            10);

  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("metadata")
                .struct_value()
                .fields()
                .at("foo")
                .string_value(),
            "bar");
}

TEST_F(A2aJsonParserTest, ParseTasksList) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tasks/list",
    "id": "125",
    "params": {
      "tenant": "mytenant",
      "contextId": "ctx-123",
      "status": "working",
      "pageSize": 50,
      "pageToken": "token123",
      "historyLength": 5,
      "lastUpdatedAfter": 1234567890,
      "includeArtifacts": true
    }
  })";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/list");
  EXPECT_EQ(parser_.metadata().fields().at("id").string_value(), "125");
  EXPECT_EQ(parser_.metadata().fields().at("method").string_value(), "tasks/list");
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("tenant").string_value(),
      "mytenant");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("contextId")
                .string_value(),
            "ctx-123");
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("status").string_value(),
      "working");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("pageSize")
                .number_value(),
            50);
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("pageToken")
                .string_value(),
            "token123");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("historyLength")
                .number_value(),
            5);
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("lastUpdatedAfter")
                .number_value(),
            1234567890);
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("includeArtifacts")
                .bool_value(),
            true);
}

TEST_F(A2aJsonParserTest, ParseTasksPushNotificationConfigSet) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tasks/pushNotificationConfig/set",
    "id": "126",
    "params": {
      "taskId": "task123",
      "pushNotificationConfig": {
        "id": "config1",
        "url": "https://example.com/notify",
        "token": "secret-token",
        "authentication": {
          "schemes": ["Bearer"],
          "credentials": "abc"
        }
      }
    }
  })";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/pushNotificationConfig/set");
  EXPECT_EQ(parser_.metadata().fields().at("id").string_value(), "126");
  EXPECT_EQ(parser_.metadata().fields().at("method").string_value(),
            "tasks/pushNotificationConfig/set");
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("taskId").string_value(),
      "task123");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("pushNotificationConfig")
                .struct_value()
                .fields()
                .at("id")
                .string_value(),
            "config1");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("pushNotificationConfig")
                .struct_value()
                .fields()
                .at("url")
                .string_value(),
            "https://example.com/notify");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("pushNotificationConfig")
                .struct_value()
                .fields()
                .at("token")
                .string_value(),
            "secret-token");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("pushNotificationConfig")
                .struct_value()
                .fields()
                .at("authentication")
                .struct_value()
                .fields()
                .at("schemes")
                .list_value()
                .values(0)
                .string_value(),
            "Bearer");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("pushNotificationConfig")
                .struct_value()
                .fields()
                .at("authentication")
                .struct_value()
                .fields()
                .at("credentials")
                .string_value(),
            "abc");
}

// TODO(tyxia) Handle unrecognized methods/fields.
TEST_F(A2aJsonParserTest, ParseUnrecognizedMethod) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "unknown/method",
    "id": "123",
    "params": {
      "someField": "someValue"
    }
  })";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "unknown/method");
  EXPECT_EQ(parser_.metadata().fields().at("id").string_value(), "123");
  // params should not be extracted for unknown method
  EXPECT_FALSE(parser_.metadata().fields().contains("params"));
}

TEST_F(A2aJsonParserTest, InvalidJson) {
  // Invalid JSON (truncated)
  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "message/send",
    "id": "123",
    "params": {
  )";

  // The parse call itself will succeed since it is streaming and waiting for more data,
  // but finishParse should definitely fail.
  ASSERT_TRUE(parser_.parse(json).ok());
  EXPECT_FALSE(parser_.finishParse().ok());
}

TEST_F(A2aJsonParserTest, MissingJsonRpc) {
  const std::string json = R"({
    "method": "message/send",
    "id": "123",
    "params": {}
  })";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  // Should return false because 'jsonrpc' field is missing from extracted metadata
  EXPECT_FALSE(parser_.isValidA2aRequest());
}

TEST_F(A2aJsonParserTest, ParseTasksListMissingOptionalFields) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "tasks/list",
    "params": {
      "tenant": "mytenant",
      "contextId": "ctx-123",
      "status": "working",
      "pageSize": 50,
      "pageToken": "token123",
      "lastUpdatedAfter": 1234567890,
      "includeArtifacts": true
    }
  })";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/list");
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("tenant").string_value(),
      "mytenant");

  // Verify historyLength is missing
  EXPECT_FALSE(
      parser_.metadata().fields().at("params").struct_value().fields().contains("historyLength"));
}

TEST_F(A2aJsonParserTest, GetTaskRequest) {
  const std::string json = R"({
  "jsonrpc": "2.0",
  "id": 102,
  "method": "tasks/get",
  "params": {
    "id": "task-uuid-12345",
    "historyLength": 10,
    "metadata": {
      "request_source": "status_check_button"
    }
  }
})";
  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/get");
  EXPECT_EQ(parser_.metadata().fields().at("id").number_value(), 102);
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("id").string_value(),
      "task-uuid-12345");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("historyLength")
                .number_value(),
            10);
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("metadata")
                .struct_value()
                .fields()
                .at("request_source")
                .string_value(),
            "status_check_button");
}

TEST_F(A2aJsonParserTest, CancelTaskRequest) {
  const std::string json = R"({
  "jsonrpc": "2.0",
  "id": 103,
  "method": "tasks/cancel",
  "params": {
    "id": "task-uuid-12345",
    "metadata": {
      "reason": "User initiated cancellation"
    }
  }
})";
  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/cancel");
  EXPECT_EQ(parser_.metadata().fields().at("id").number_value(), 103);
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("id").string_value(),
      "task-uuid-12345");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("metadata")
                .struct_value()
                .fields()
                .at("reason")
                .string_value(),
            "User initiated cancellation");
}

TEST_F(A2aJsonParserTest, ResubscribeTaskRequest) {
  const std::string json = R"({
  "jsonrpc": "2.0",
  "id": 106,
  "method": "tasks/resubscribe",
  "params": {
    "id": "task-uuid-67890",
    "historyLength": 2,
    "metadata": {
      "client_state": "reconnecting"
    }
  }
})";
  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/resubscribe");
  EXPECT_EQ(parser_.metadata().fields().at("id").number_value(), 106);
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("id").string_value(),
      "task-uuid-67890");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("metadata")
                .struct_value()
                .fields()
                .at("client_state")
                .string_value(),
            "reconnecting");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("historyLength")
                .number_value(),
            2);
}

TEST_F(A2aJsonParserTest, ParseTasksPushNotificationConfigGet) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "id": 130,
    "method": "tasks/pushNotificationConfig/get",
    "params": {
      "id": "task-uuid-12345",
      "metadata": {"foo": "bar"},
      "pushNotificationConfigId": "config-abc"
    }
  })";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/pushNotificationConfig/get");
  EXPECT_EQ(parser_.metadata().fields().at("id").number_value(), 130);
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("id").string_value(),
      "task-uuid-12345");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("metadata")
                .struct_value()
                .fields()
                .at("foo")
                .string_value(),
            "bar");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("pushNotificationConfigId")
                .string_value(),
            "config-abc");
}

TEST_F(A2aJsonParserTest, ParseTasksPushNotificationConfigList) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "id": 131,
    "method": "tasks/pushNotificationConfig/list",
    "params": {
      "id": "task-uuid-12345",
      "metadata": {"foo": "bar"},
      "pushNotificationConfigId": "config-abc"
    }
  })";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/pushNotificationConfig/list");
  EXPECT_EQ(parser_.metadata().fields().at("id").number_value(), 131);
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("id").string_value(),
      "task-uuid-12345");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("metadata")
                .struct_value()
                .fields()
                .at("foo")
                .string_value(),
            "bar");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("pushNotificationConfigId")
                .string_value(),
            "config-abc");
}

TEST_F(A2aJsonParserTest, ParseTasksPushNotificationConfigDelete) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "id": 132,
    "method": "tasks/pushNotificationConfig/delete",
    "params": {
      "id": "task-uuid-12345",
      "pushNotificationConfigId": "config-abc"
    }
  })";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/pushNotificationConfig/delete");
  EXPECT_EQ(parser_.metadata().fields().at("id").number_value(), 132);
  EXPECT_EQ(
      parser_.metadata().fields().at("params").struct_value().fields().at("id").string_value(),
      "task-uuid-12345");
  EXPECT_EQ(parser_.metadata()
                .fields()
                .at("params")
                .struct_value()
                .fields()
                .at("pushNotificationConfigId")
                .string_value(),
            "config-abc");
}

TEST_F(A2aJsonParserTest, ParseAgentGetAuthenticatedExtendedCard) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "id": 133,
    "method": "agent/getAuthenticatedExtendedCard",
    "params": {}
  })";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "agent/getAuthenticatedExtendedCard");
  EXPECT_EQ(parser_.metadata().fields().at("id").number_value(), 133);
}

TEST_F(A2aJsonParserTest, GetNestedValue) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "method": "message/send",
    "id": "123",
    "params": {
      "taskId": "task-abc-987",
      "message": {
        "role": "user",
        "kind": "message"
      },
      "configuration": {
        "blocking": true
      }
    }
  })";
  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());

  // Valid paths
  EXPECT_EQ(parser_.getNestedValue("params.taskId")->string_value(), "task-abc-987");
  EXPECT_EQ(parser_.getNestedValue("params.message.role")->string_value(), "user");
  EXPECT_TRUE(parser_.getNestedValue("params.configuration.blocking")->bool_value());

  // Invalid paths
  EXPECT_EQ(parser_.getNestedValue(""), nullptr);
  EXPECT_EQ(parser_.getNestedValue("params.message.foo"), nullptr);
  EXPECT_EQ(parser_.getNestedValue("params.taskId.foo"), nullptr);
  EXPECT_EQ(parser_.getNestedValue("invalid.path"), nullptr);
}

TEST_F(A2aJsonParserTest, Reset) {
  const std::string json1 =
      R"({"jsonrpc": "2.0", "method": "tasks/get", "id": "1", "params": {"id": "task1"}})";
  const std::string json2 =
      R"({"jsonrpc": "2.0", "method": "tasks/cancel", "id": "2", "params": {"id": "task2"}})";

  ASSERT_TRUE(parser_.parse(json1).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/get");
  EXPECT_EQ(parser_.metadata().fields().at("id").string_value(), "1");

  parser_.reset();
  EXPECT_FALSE(parser_.isValidA2aRequest());
  EXPECT_TRUE(parser_.metadata().fields().empty());

  ASSERT_TRUE(parser_.parse(json2).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/cancel");
  EXPECT_EQ(parser_.metadata().fields().at("id").string_value(), "2");
}

// TODO(tyxia): Add support for parsing responses.
TEST_F(A2aJsonParserTest, ParseResponseWithResult) {
  const std::string json = R"({
  "jsonrpc": "2.0",
  "id": "1",
  "result": {
    "kind": "task",
    "id": "run-uuid",
    "contextId": "f5bd2a40-74b6-4f7a-b649-ea3f09890003",
    "status": {
      "state": "completed"
    },
    "artifacts": [
      {
        "artifactId": "artifact-uuid",
        "name": "Assistant Response",
        "parts": [
          {
            "kind": "text",
            "text": "Hello back"
          }
        ]
      }
    ]
  }
})";

  ASSERT_TRUE(parser_.parse(json).ok());
  ASSERT_TRUE(parser_.finishParse().ok());
  EXPECT_FALSE(parser_.isValidA2aRequest());
}

} // namespace
} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
