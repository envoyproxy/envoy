#include "source/extensions/filters/http/a2a/a2a_json_parser.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/struct_matchers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AllOf;
using testing::Contains;
using testing::ElementsAre;
using testing::IsSupersetOf;
using testing::Key;
using testing::UnorderedElementsAre;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {
namespace {

using ::Envoy::StatusHelpers::IsOk;
using testing::Not;

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
  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());

  // Verify overall validity and method.
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "message/send");

  // Verify extracted fields.
  EXPECT_THAT(
      parser_.metadata().fields(),
      IsSupersetOf(StructMatchers(
          IsStructString("method", "message/send"),
          IsStructStruct(
              "params",
              UnorderedElementsAre(
                  IsStructString("taskId", "task-abc-987"),
                  IsStructStruct("message",
                                 UnorderedElementsAre(IsStructString("taskId", "task1"),
                                                      IsStructString("contextId", "context1"),
                                                      IsStructString("messageId", "msg1"),
                                                      IsStructString("role", "user"))),
                  IsStructStruct("configuration",
                                 UnorderedElementsAre(
                                     IsStructBool("blocking", true),
                                     IsStructList("acceptedOutputModes",
                                                  ElementsAre(IsStructValueString("text/plain"))))),
                  IsStructStruct("metadata",
                                 UnorderedElementsAre(IsStructString("baz", "qux"))))))));
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
  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());

  // Verify overall validity and method.
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "message/send");

  // Verify extracted fields.
  EXPECT_THAT(
      parser_.metadata().fields(),
      UnorderedElementsAre(
          IsStructString("jsonrpc", "2.0"), IsStructString("id", "123"),
          IsStructString("method", "message/send"),
          IsStructStruct(
              "params",
              UnorderedElementsAre(
                  IsStructString("taskId", "task-abc-987"),
                  IsStructStruct(
                      "message",
                      UnorderedElementsAre(
                          IsStructString("taskId", "task1"),
                          IsStructString("contextId", "context1"),
                          IsStructString("messageId", "msg1"), IsStructString("role", "user"),
                          IsStructList(
                              "parts",
                              ElementsAre(
                                  IsStructValueStruct(UnorderedElementsAre(
                                      IsStructString("type", "text"),
                                      IsStructString("text", "Can you analyze the attached CSV for "
                                                             "Q3 sales trends?"))),
                                  IsStructValueStruct(UnorderedElementsAre(
                                      IsStructString("type", "file"),
                                      IsStructStruct(
                                          "file",
                                          UnorderedElementsAre(
                                              IsStructString("mimeType", "text/csv"),
                                              IsStructString(
                                                  "uri",
                                                  "https://example.com/secure/data.csv"))))))),
                          IsStructString("kind", "message"),
                          IsStructStruct("metadata",
                                         UnorderedElementsAre(IsStructString("foo", "bar"))))),
                  IsStructStruct("configuration",
                                 UnorderedElementsAre(
                                     IsStructBool("blocking", true),
                                     IsStructList("acceptedOutputModes",
                                                  ElementsAre(IsStructValueString("text/plain"))))),
                  IsStructStruct("metadata",
                                 UnorderedElementsAre(IsStructString("baz", "qux")))))));
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

  ASSERT_OK(parser_.parse(part1));
  ASSERT_OK(parser_.parse(part2));
  ASSERT_OK(parser_.finishParse());

  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "message/send");
  EXPECT_THAT(
      parser_.metadata().fields(),
      UnorderedElementsAre(
          IsStructString("jsonrpc", "2.0"), IsStructString("method", "message/send"),
          IsStructString("id", "123"),
          IsStructStruct(
              "params",
              UnorderedElementsAre(
                  IsStructString("taskId", "task-abc-987"),
                  IsStructStruct(
                      "message",
                      UnorderedElementsAre(
                          IsStructString("taskId", "task1"),
                          IsStructString("contextId", "context1"),
                          IsStructString("messageId", "msg1"), IsStructString("role", "user"),
                          IsStructList(
                              "parts",
                              ElementsAre(IsStructValueStruct(UnorderedElementsAre(
                                  IsStructString("type", "text"),
                                  IsStructString("text",
                                                 "Can you analyze the attached CSV for Q3 sales "
                                                 "trends?")))))))))));
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

  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/get");
  EXPECT_THAT(
      parser_.metadata().fields(),
      IsSupersetOf(StructMatchers(
          IsStructString("id", "124"),
          IsStructStruct(
              "params", UnorderedElementsAre(
                            IsStructString("id", "task1"), IsStructNumber("historyLength", 10),
                            IsStructStruct("metadata",
                                           UnorderedElementsAre(IsStructString("foo", "bar"))))))));
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

  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/list");
  EXPECT_THAT(parser_.metadata().fields(),
              UnorderedElementsAre(
                  IsStructString("jsonrpc", "2.0"), IsStructString("id", "125"),
                  IsStructString("method", "tasks/list"),
                  IsStructStruct(
                      "params", UnorderedElementsAre(IsStructString("tenant", "mytenant"),
                                                     IsStructString("contextId", "ctx-123"),
                                                     IsStructString("status", "working"),
                                                     IsStructNumber("pageSize", 50),
                                                     IsStructString("pageToken", "token123"),
                                                     IsStructNumber("historyLength", 5),
                                                     IsStructNumber("lastUpdatedAfter", 1234567890),
                                                     IsStructBool("includeArtifacts", true)))));
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

  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/pushNotificationConfig/set");
  EXPECT_THAT(parser_.metadata().fields(),
              UnorderedElementsAre(
                  IsStructString("jsonrpc", "2.0"), IsStructString("id", "126"),
                  IsStructString("method", "tasks/pushNotificationConfig/set"),
                  IsStructStruct(
                      "params",
                      UnorderedElementsAre(
                          IsStructString("taskId", "task123"),
                          IsStructStruct(
                              "pushNotificationConfig",
                              UnorderedElementsAre(
                                  IsStructString("id", "config1"),
                                  IsStructString("url", "https://example.com/notify"),
                                  IsStructString("token", "secret-token"),
                                  IsStructStruct(
                                      "authentication",
                                      UnorderedElementsAre(
                                          IsStructList("schemes",
                                                       ElementsAre(IsStructValueString("Bearer"))),
                                          IsStructString("credentials", "abc")))))))));
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
  ASSERT_OK(parser_.parse(json));
  EXPECT_THAT(parser_.finishParse(), Not(IsOk()));
}

TEST_F(A2aJsonParserTest, MissingJsonRpc) {
  const std::string json = R"({
    "method": "message/send",
    "id": "123",
    "params": {}
  })";

  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
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

  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/list");
  // historyLength should not be present.
  EXPECT_THAT(
      parser_.metadata().fields(),
      Contains(IsStructStruct("params", AllOf(Contains(IsStructString("tenant", "mytenant")),
                                              Not(Contains(Key("historyLength")))))));
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
  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/get");
  EXPECT_THAT(
      parser_.metadata().fields(),
      UnorderedElementsAre(
          IsStructString("jsonrpc", "2.0"), IsStructNumber("id", 102),
          IsStructString("method", "tasks/get"),
          IsStructStruct(
              "params",
              UnorderedElementsAre(
                  IsStructString("id", "task-uuid-12345"), IsStructNumber("historyLength", 10),
                  IsStructStruct("metadata", UnorderedElementsAre(IsStructString(
                                                 "request_source", "status_check_button")))))));
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
  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/cancel");
  EXPECT_THAT(
      parser_.metadata().fields(),
      UnorderedElementsAre(
          IsStructString("jsonrpc", "2.0"), IsStructNumber("id", 103),
          IsStructString("method", "tasks/cancel"),
          IsStructStruct(
              "params",
              UnorderedElementsAre(
                  IsStructString("id", "task-uuid-12345"),
                  IsStructStruct("metadata", UnorderedElementsAre(IsStructString(
                                                 "reason", "User initiated cancellation")))))));
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
  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/resubscribe");
  EXPECT_THAT(
      parser_.metadata().fields(),
      UnorderedElementsAre(
          IsStructString("jsonrpc", "2.0"), IsStructNumber("id", 106),
          IsStructString("method", "tasks/resubscribe"),
          IsStructStruct("params",
                         UnorderedElementsAre(
                             IsStructString("id", "task-uuid-67890"),
                             IsStructNumber("historyLength", 2),
                             IsStructStruct("metadata", UnorderedElementsAre(IsStructString(
                                                            "client_state", "reconnecting")))))));
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

  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/pushNotificationConfig/get");
  EXPECT_THAT(parser_.metadata().fields(),
              UnorderedElementsAre(
                  IsStructString("jsonrpc", "2.0"), IsStructNumber("id", 130),
                  IsStructString("method", "tasks/pushNotificationConfig/get"),
                  IsStructStruct("params",
                                 UnorderedElementsAre(
                                     IsStructString("id", "task-uuid-12345"),
                                     IsStructStruct("metadata", UnorderedElementsAre(
                                                                    IsStructString("foo", "bar"))),
                                     IsStructString("pushNotificationConfigId", "config-abc")))));
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

  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/pushNotificationConfig/list");
  EXPECT_THAT(parser_.metadata().fields(),
              UnorderedElementsAre(
                  IsStructString("jsonrpc", "2.0"), IsStructNumber("id", 131),
                  IsStructString("method", "tasks/pushNotificationConfig/list"),
                  IsStructStruct("params",
                                 UnorderedElementsAre(
                                     IsStructString("id", "task-uuid-12345"),
                                     IsStructStruct("metadata", UnorderedElementsAre(
                                                                    IsStructString("foo", "bar"))),
                                     IsStructString("pushNotificationConfigId", "config-abc")))));
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

  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/pushNotificationConfig/delete");
  EXPECT_THAT(
      parser_.metadata().fields(),
      UnorderedElementsAre(
          IsStructString("jsonrpc", "2.0"), IsStructNumber("id", 132),
          IsStructString("method", "tasks/pushNotificationConfig/delete"),
          IsStructStruct("params", UnorderedElementsAre(
                                       IsStructString("id", "task-uuid-12345"),
                                       IsStructString("pushNotificationConfigId", "config-abc")))));
}

TEST_F(A2aJsonParserTest, ParseAgentGetAuthenticatedExtendedCard) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "id": 133,
    "method": "agent/getAuthenticatedExtendedCard",
    "params": {}
  })";

  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "agent/getAuthenticatedExtendedCard");
  EXPECT_THAT(parser_.metadata().fields(), Contains(IsStructNumber("id", 133)));
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
  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());

  // Valid paths.
  EXPECT_THAT(*parser_.getNestedValue("params.taskId"), IsStructValueString("task-abc-987"));
  EXPECT_THAT(*parser_.getNestedValue("params.message.role"), IsStructValueString("user"));
  EXPECT_THAT(*parser_.getNestedValue("params.configuration.blocking"), IsStructValueBool(true));

  // Invalid paths.
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

  ASSERT_OK(parser_.parse(json1));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/get");
  EXPECT_THAT(parser_.metadata().fields(), Contains(IsStructString("id", "1")));

  parser_.reset();
  EXPECT_FALSE(parser_.isValidA2aRequest());
  EXPECT_TRUE(parser_.metadata().fields().empty());

  ASSERT_OK(parser_.parse(json2));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_EQ(parser_.getMethod(), "tasks/cancel");
  EXPECT_THAT(parser_.metadata().fields(), Contains(IsStructString("id", "2")));
}

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

  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_THAT(parser_.metadata().fields(),
              UnorderedElementsAre(
                  IsStructString("jsonrpc", "2.0"), IsStructString("id", "1"),
                  IsStructStruct(
                      "result",
                      UnorderedElementsAre(
                          IsStructString("kind", "task"), IsStructString("id", "run-uuid"),
                          IsStructString("contextId", "f5bd2a40-74b6-4f7a-b649-ea3f09890003"),
                          IsStructStruct(
                              "status", UnorderedElementsAre(IsStructString("state", "completed"))),
                          IsStructList(
                              "artifacts",
                              ElementsAre(IsStructValueStruct(UnorderedElementsAre(
                                  IsStructString("artifactId", "artifact-uuid"),
                                  IsStructString("name", "Assistant Response"),
                                  IsStructList("parts",
                                               ElementsAre(IsStructValueStruct(UnorderedElementsAre(
                                                   IsStructString("kind", "text"),
                                                   IsStructString("text", "Hello back")))))))))))));
}

TEST_F(A2aJsonParserTest, GetTaskErrorResponse) {
  const std::string json = R"({
    "jsonrpc": "2.0",
    "id": 102,
    "result": null,
    "error": {
        "code": -32001,
        "message": "Task not found",
        "data": null
    }
    })";
  ASSERT_OK(parser_.parse(json));
  ASSERT_OK(parser_.finishParse());
  EXPECT_TRUE(parser_.isValidA2aRequest());
  EXPECT_THAT(parser_.metadata().fields(),
              UnorderedElementsAre(
                  IsStructString("jsonrpc", "2.0"), IsStructNumber("id", 102),
                  IsStructNull("result", Protobuf::NULL_VALUE),
                  IsStructStruct(
                      "error", UnorderedElementsAre(IsStructNumber("code", -32001),
                                                    IsStructString("message", "Task not found"),
                                                    IsStructNull("data", Protobuf::NULL_VALUE)))));
}

} // namespace
} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
