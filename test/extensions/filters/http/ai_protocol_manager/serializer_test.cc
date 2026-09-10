#include <memory>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::HasStatusCode;

class SerializerTest : public testing::Test {
public:
  SerializerTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")), factory_(),
        bridge_raw_(new FakeBridge(*dispatcher_)),
        buffer_manager_(factory_, std::unique_ptr<FakeBridge>(bridge_raw_)) {}

  ~SerializerTest() override { buffer_manager_.onDestroy(); }

  void drain() {
    for (int i = 0; i < 10; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  absl::StatusOr<JsonWithExtBuf> runSerialize(const JsonWithExtBuf& doc,
                                              BufferManager* buffer_manager) {
    auto executor = std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_);
    absl::StatusOr<JsonWithExtBuf> final_result;
    bool completed = false;

    auto handle = Coroutine::launch(
        Serializer::serialize(doc, buffer_manager), executor,
        [&completed, &final_result](absl::StatusOr<JsonWithExtBuf> result) {
          final_result = std::move(result);
          completed = true;
        },
        Coroutine::StartMode::Inline);

    drain();
    EXPECT_TRUE(completed);
    return final_result;
  }

  absl::StatusOr<Serializer::SerializedOffsets> runCalculateOffsets(const JsonWithExtBuf& doc) {
    auto executor = std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_);
    absl::StatusOr<Serializer::SerializedOffsets> final_result;
    bool completed = false;

    auto handle = Coroutine::launch(
        Serializer::calculateSerializedOffsets(doc), executor,
        [&completed, &final_result](absl::StatusOr<Serializer::SerializedOffsets> result) {
          final_result = std::move(result);
          completed = true;
        },
        Coroutine::StartMode::Inline);

    drain();
    EXPECT_TRUE(completed);
    return final_result;
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  FakeBridge* bridge_raw_{nullptr};
  BufferManager buffer_manager_;
};

TEST_F(SerializerTest, PureJsonSerialization) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{
      {"model", "gpt-4"},
      {"temperature", 0.7},
      {"stream", false},
      {"tags", {"a", "b", "c"}},
      {"extra", nullptr},
  });

  auto result_or = runSerialize(doc, &buffer_manager_);
  ASSERT_OK(result_or);
  std::string output = bridge_raw_->injected_.toString();

  auto parsed = nlohmann::json::parse(output);
  EXPECT_EQ(parsed["model"], "gpt-4");
  EXPECT_DOUBLE_EQ(parsed["temperature"].get<double>(), 0.7);
  EXPECT_EQ(parsed["stream"], false);
  EXPECT_EQ(parsed["tags"], (std::vector<std::string>{"a", "b", "c"}));
  EXPECT_TRUE(parsed["extra"].is_null());

  auto offset_doc_or = runCalculateOffsets(doc);
  ASSERT_OK(offset_doc_or);
  EXPECT_EQ(offset_doc_or->total_size, output.size());
}

TEST_F(SerializerTest, ExternalRefSerializationAndOffsetRecalculation) {
  std::string secret = "This is a very long offloaded prompt text.";
  Buffer::OwnedImpl secret_buf(secret);
  buffer_manager_.onData(secret_buf);
  buffer_manager_.endStream();
  drain();

  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{
      {"model", "claude-3"},
      {"prompt", JsonWithExtBuf::makeExternalRef({0, secret.size()})},
  });

  auto result_or = runSerialize(doc, &buffer_manager_);
  ASSERT_OK(result_or);
  std::string output = bridge_raw_->injected_.toString();

  auto parsed = nlohmann::json::parse(output);
  EXPECT_EQ(parsed["model"], "claude-3");
  EXPECT_EQ(parsed["prompt"], secret);

  auto offset_doc_or = runCalculateOffsets(doc);
  ASSERT_OK(offset_doc_or);
  EXPECT_EQ(offset_doc_or->total_size, output.size());
  const auto& new_json = offset_doc_or->doc.json();
  ASSERT_TRUE(JsonWithExtBuf::isExternalRef(new_json["prompt"]));
  auto ref = *JsonWithExtBuf::externalRef(new_json["prompt"]);
  EXPECT_EQ(ref.length, secret.size());

  // Verify the calculated offset matches the exact substring in output
  EXPECT_LE(ref.offset + ref.length, output.size());
  EXPECT_EQ(output.substr(ref.offset, ref.length), secret);
}

TEST_F(SerializerTest, ExternalRefNullBufferFails) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{
      {"prompt", JsonWithExtBuf::makeExternalRef({0, 10})},
  });

  auto result_or = runSerialize(doc, nullptr);
  EXPECT_THAT(result_or.status(), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST_F(SerializerTest, ExternalRefOutOfBoundsFails) {
  std::string secret = "hello world";
  Buffer::OwnedImpl secret_buf(secret);
  buffer_manager_.onData(secret_buf);
  buffer_manager_.endStream();
  drain();

  // ref offset + length exceeds buffer length (11 bytes)
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{
      {"prompt", JsonWithExtBuf::makeExternalRef({5, 100})},
  });

  auto result_or = runSerialize(doc, &buffer_manager_);
  EXPECT_THAT(result_or.status(), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(result_or.status().message(), testing::HasSubstr("exceeds buffer length"));
}

TEST_F(SerializerTest, ExternalRefOffsetOutOfBoundsFails) {
  std::string secret = "hello world";
  Buffer::OwnedImpl secret_buf(secret);
  buffer_manager_.onData(secret_buf);
  buffer_manager_.endStream();
  drain();

  // ref offset itself exceeds buffer length (11 bytes)
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{
      {"prompt", JsonWithExtBuf::makeExternalRef({100, 0})},
  });

  auto result_or = runSerialize(doc, &buffer_manager_);
  EXPECT_THAT(result_or.status(), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

TEST_F(SerializerTest, NestedStructureWithMultipleRefs) {
  std::string part1 = "System instructions";
  std::string part2 = "User query text";

  Buffer::OwnedImpl part1_buf(part1);
  buffer_manager_.onData(part1_buf);
  Buffer::OwnedImpl part2_buf(part2);
  buffer_manager_.onData(part2_buf);
  buffer_manager_.endStream();
  drain();

  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{
      {"messages",
       nlohmann::json::array({
           {{"role", "system"}, {"content", JsonWithExtBuf::makeExternalRef({0, part1.size()})}},
           {{"role", "user"},
            {"content", JsonWithExtBuf::makeExternalRef({part1.size(), part2.size()})}},
       })},
  });

  auto result_or = runSerialize(doc, &buffer_manager_);
  ASSERT_OK(result_or);
  std::string output = bridge_raw_->injected_.toString();

  auto parsed = nlohmann::json::parse(output);
  ASSERT_TRUE(parsed["messages"].is_array());
  EXPECT_EQ(parsed["messages"][0]["content"], part1);
  EXPECT_EQ(parsed["messages"][1]["content"], part2);

  auto offset_doc_or = runCalculateOffsets(doc);
  ASSERT_OK(offset_doc_or);
  EXPECT_EQ(offset_doc_or->total_size, output.size());
  const auto& new_json = offset_doc_or->doc.json();
  auto ref1 = *JsonWithExtBuf::externalRef(new_json["messages"][0]["content"]);
  auto ref2 = *JsonWithExtBuf::externalRef(new_json["messages"][1]["content"]);

  EXPECT_EQ(output.substr(ref1.offset, ref1.length), part1);
  EXPECT_EQ(output.substr(ref2.offset, ref2.length), part2);
}

TEST_F(SerializerTest, LargeExternalRefMultiChunk) {
  std::string large_payload(150 * 1024, 'x'); // 150KB > 2 chunks of 64KB
  Buffer::OwnedImpl large_buf(large_payload);
  buffer_manager_.onData(large_buf);
  buffer_manager_.endStream();
  drain();

  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{
      {"large_field", JsonWithExtBuf::makeExternalRef({0, large_payload.size()})},
  });

  auto result_or = runSerialize(doc, &buffer_manager_);
  ASSERT_OK(result_or);

  std::string output = bridge_raw_->injected_.toString();
  auto ref = *JsonWithExtBuf::externalRef(result_or->json()["large_field"]);
  EXPECT_EQ(ref.length, large_payload.size());
  EXPECT_EQ(output.substr(ref.offset, ref.length), large_payload);
}

TEST_F(SerializerTest, SpecialCharactersEscaping) {
  JsonWithExtBuf doc;
  doc.setJson(nlohmann::json{
      {"quote\"key", "value\nwith\tspecial \"quotes\" and /slashes/ and \\backslashes\\"},
  });

  auto result_or = runSerialize(doc, &buffer_manager_);
  ASSERT_OK(result_or);
  std::string output = bridge_raw_->injected_.toString();

  auto parsed = nlohmann::json::parse(output);
  EXPECT_EQ(parsed["quote\"key"],
            "value\nwith\tspecial \"quotes\" and /slashes/ and \\backslashes\\");
}

TEST_F(SerializerTest, InvalidUtf8StringReturnsError) {
  JsonWithExtBuf doc;
  std::string invalid_utf8 = "invalid \xff\xff byte";
  doc.setJson(nlohmann::json{
      {"invalid_str", invalid_utf8},
  });

  auto result_or = runSerialize(doc, &buffer_manager_);
  EXPECT_FALSE(result_or.ok());
  EXPECT_THAT(result_or.status(), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(result_or.status().message(), testing::HasSubstr("JSON serialization error"));

  auto offset_or = runCalculateOffsets(doc);
  EXPECT_FALSE(offset_or.ok());
  EXPECT_THAT(offset_or.status(), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(offset_or.status().message(), testing::HasSubstr("JSON serialization error"));
}

TEST_F(SerializerTest, InvalidUtf8KeyReturnsError) {
  JsonWithExtBuf doc;
  std::string invalid_utf8_key = "invalid \xff\xff key";
  nlohmann::json j = nlohmann::json::object();
  j[invalid_utf8_key] = "value";
  doc.setJson(std::move(j));

  auto result_or = runSerialize(doc, &buffer_manager_);
  EXPECT_FALSE(result_or.ok());
  EXPECT_THAT(result_or.status(), HasStatusCode(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(result_or.status().message(), testing::HasSubstr("JSON serialization error"));
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
