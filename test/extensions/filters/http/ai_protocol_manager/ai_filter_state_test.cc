#include "envoy/registry/registry.h"
#include "envoy/router/string_accessor.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter_state.h"

#include "absl/types/variant.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

class RequestLlmProtocolFactoryTest : public testing::Test {
protected:
  void SetUp() override {
    factory_ = Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(
        FilterStateKeys::LlmProtocolRequest);
    ASSERT_NE(factory_, nullptr);
  }

  LLMProtocol build(absl::string_view name) {
    const auto object = factory_->createFromBytes(name);
    EXPECT_NE(object, nullptr) << name;
    const auto* typed = dynamic_cast<const RequestLlmProtocol*>(object.get());
    EXPECT_NE(typed, nullptr);
    return typed != nullptr ? typed->protocol() : LLMProtocol::Unspecified;
  }

  const StreamInfo::FilterState::ObjectFactory* factory_{nullptr};
};

TEST(RequestLlmProtocolTest, SerializesAsEnumValueName) {
  EXPECT_EQ(RequestLlmProtocol(LLMProtocol::GeminiGenerateContent).serializeAsString(),
            "GEMINI_GENERATE_CONTENT");
  EXPECT_EQ(RequestLlmProtocol(LLMProtocol::Unspecified).serializeAsString(),
            "LLM_PROTOCOL_UNSPECIFIED");
}

TEST(RequestLlmProtocolTest, ExposesLlmProtocolField) {
  const RequestLlmProtocol object(LLMProtocol::OpenAiChatCompletions);
  EXPECT_TRUE(object.hasFieldSupport());
  EXPECT_EQ(absl::get<absl::string_view>(object.getField("llm_protocol")),
            "OPENAI_CHAT_COMPLETIONS");
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(object.getField("model")));
}

TEST_F(RequestLlmProtocolFactoryTest, BuildsFromEnumValueName) {
  EXPECT_EQ(build("OPENAI_RESPONSES"), LLMProtocol::OpenAiResponses);
  EXPECT_EQ(build("ANTHROPIC_MESSAGES"), LLMProtocol::AnthropicMessages);
  EXPECT_EQ(build("LLM_PROTOCOL_UNSPECIFIED"), LLMProtocol::Unspecified);
}

TEST_F(RequestLlmProtocolFactoryTest, RejectsUnknownName) {
  EXPECT_EQ(factory_->createFromBytes("openai_chat_completions"), nullptr);
  EXPECT_EQ(factory_->createFromBytes("NOT_AN_API"), nullptr);
  EXPECT_EQ(factory_->createFromBytes(""), nullptr);
}

TEST(RequestLlmProtocolTest, ReadsBackFromFilterState) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(RequestLlmProtocol::fromFilterState(filter_state), LLMProtocol::Unspecified);

  filter_state.setData(FilterStateKeys::LlmProtocolRequest,
                       std::make_shared<RequestLlmProtocol>(LLMProtocol::AnthropicMessages),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(RequestLlmProtocol::fromFilterState(filter_state), LLMProtocol::AnthropicMessages);
}

TEST(RequestModelFactoryTest, BuildsStringAccessorFromModelName) {
  const auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(
          FilterStateKeys::ModelRequest);
  ASSERT_NE(factory, nullptr);
  const auto object = factory->createFromBytes("gpt-4o-mini");
  const auto* model = dynamic_cast<const Router::StringAccessor*>(object.get());
  ASSERT_NE(model, nullptr);
  EXPECT_EQ(model->asString(), "gpt-4o-mini");
  EXPECT_EQ(object->serializeAsString(), "gpt-4o-mini");
}

TEST(RequestModelFactoryTest, RejectsEmptyModel) {
  const auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(
          FilterStateKeys::ModelRequest);
  ASSERT_NE(factory, nullptr);
  EXPECT_EQ(factory->createFromBytes(""), nullptr);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
