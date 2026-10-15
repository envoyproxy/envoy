#include "envoy/registry/registry.h"
#include "envoy/router/string_accessor.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/router/string_accessor_impl.h"
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

TEST(ResponseLlmProtocolTest, FactoryBuildsFromEnumValueNameAndRejectsUnknown) {
  const auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(
          FilterStateKeys::LlmProtocolResponse);
  ASSERT_NE(factory, nullptr);
  const auto object = factory->createFromBytes("GEMINI_GENERATE_CONTENT");
  ASSERT_NE(object, nullptr);
  const auto* typed = dynamic_cast<const ResponseLlmProtocol*>(object.get());
  ASSERT_NE(typed, nullptr);
  EXPECT_EQ(typed->protocol(), LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(object->serializeAsString(), "GEMINI_GENERATE_CONTENT");
  EXPECT_EQ(factory->createFromBytes("NOT_AN_API"), nullptr);
}

// The request and response keys are read independently: setting one leaves the other unset.
TEST(ResponseLlmProtocolTest, ReadsItsOwnKeyOnly) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(ResponseLlmProtocol::fromFilterState(filter_state), LLMProtocol::Unspecified);

  filter_state.setData(FilterStateKeys::LlmProtocolResponse,
                       std::make_shared<ResponseLlmProtocol>(LLMProtocol::AnthropicMessages),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(ResponseLlmProtocol::fromFilterState(filter_state), LLMProtocol::AnthropicMessages);
  EXPECT_EQ(RequestLlmProtocol::fromFilterState(filter_state), LLMProtocol::Unspecified);
}

// The backend API in effect: filter state when set, else whatever the route declared.
TEST(ResponseLlmProtocolTest, ResolvesFilterStateOverTheRoute) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(ResponseLlmProtocol::resolve(filter_state, LLMProtocol::GeminiGenerateContent),
            LLMProtocol::GeminiGenerateContent);
  EXPECT_EQ(ResponseLlmProtocol::resolve(filter_state, LLMProtocol::Unspecified),
            LLMProtocol::Unspecified);

  filter_state.setData(FilterStateKeys::LlmProtocolResponse,
                       std::make_shared<ResponseLlmProtocol>(LLMProtocol::AnthropicMessages),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(ResponseLlmProtocol::resolve(filter_state, LLMProtocol::GeminiGenerateContent),
            LLMProtocol::AnthropicMessages);
}

class StringKeyFactoryTest : public testing::TestWithParam<absl::string_view> {};

INSTANTIATE_TEST_SUITE_P(StringKeys, StringKeyFactoryTest,
                         testing::Values(FilterStateKeys::ModelResolved));

TEST_P(StringKeyFactoryTest, BuildsStringAccessorAndRejectsEmpty) {
  const auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(GetParam());
  ASSERT_NE(factory, nullptr) << GetParam();
  const auto object = factory->createFromBytes("/v1beta/models/{model}:{method}");
  const auto* value = dynamic_cast<const Router::StringAccessor*>(object.get());
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(value->asString(), "/v1beta/models/{model}:{method}");
  EXPECT_EQ(factory->createFromBytes(""), nullptr);
}

class BindingFactoryTest : public testing::TestWithParam<absl::string_view> {
protected:
  std::unique_ptr<StreamInfo::FilterState::Object> build(absl::string_view value) {
    const auto* factory =
        Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(GetParam());
    EXPECT_NE(factory, nullptr) << GetParam();
    return factory != nullptr ? factory->createFromBytes(value) : nullptr;
  }
};

INSTANTIATE_TEST_SUITE_P(BindingKeys, BindingFactoryTest,
                         testing::Values(FilterStateKeys::BindingRequest,
                                         FilterStateKeys::BindingResponse));

TEST_P(BindingFactoryTest, BuildsFromAPatternOrJson) {
  constexpr absl::string_view pattern = "/gateway/{model}/{chat|chat-stream}";
  std::unique_ptr<StreamInfo::FilterState::Object> object = build(pattern);
  const auto* binding = dynamic_cast<const LlmBindingObject*>(object.get());
  ASSERT_NE(binding, nullptr);
  ASSERT_TRUE(binding->binding().ok()) << binding->binding().status();
  ASSERT_NE(*binding->binding(), nullptr);
  EXPECT_EQ((*binding->binding())->pattern()->source(), pattern);
  EXPECT_TRUE((*binding->binding())->mutations().empty());
  EXPECT_EQ(binding->asString(), pattern);
  EXPECT_EQ(object->serializeAsString(), pattern);

  // The value is kept as written, mutations and all.
  constexpr absl::string_view json =
      R"({"uri_pattern": "/v1/models/{model}:rawPredict", "mutations": [{"body_field": "model"},)"
      R"( {"body_field": "anthropic_version", "value": "v", "keep_existing": true}]})";
  object = build(json);
  binding = dynamic_cast<const LlmBindingObject*>(object.get());
  ASSERT_NE(binding, nullptr);
  ASSERT_TRUE(binding->binding().ok()) << binding->binding().status();
  ASSERT_EQ((*binding->binding())->mutations().size(), 2);
  EXPECT_TRUE((*binding->binding())->removesModel());
  EXPECT_EQ(binding->asString(), json);
  EXPECT_EQ(object->serializeAsString(), json);
}

// Only an empty value leaves the key unset. Any other value sets it, so one that does not parse
// fails the request where it is read instead of being dropped silently.
TEST_P(BindingFactoryTest, KeepsTheErrorOfAValueThatDoesNotParse) {
  EXPECT_EQ(build(""), nullptr);
  for (absl::string_view value :
       {"gateway/{model}", "/v1/{model", R"({"uri_pattern": 1})", R"({"unknown": true})",
        R"({"mutations": [{}]})", R"({"mutations": [{"header": "x-a", "value": "b"}]})"}) {
    std::unique_ptr<StreamInfo::FilterState::Object> object = build(value);
    const auto* binding = dynamic_cast<const LlmBindingObject*>(object.get());
    ASSERT_NE(binding, nullptr) << value;
    EXPECT_EQ(binding->binding().status().code(), absl::StatusCode::kInvalidArgument) << value;
    EXPECT_EQ(binding->asString(), value);
  }
}

TEST(LlmBindingObjectTest, ReadsWhatAnyWriterLeftAtTheKey) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  absl::StatusOr<LlmBindingConstSharedPtr> binding =
      LlmBindingObject::fromFilterState(filter_state, FilterStateKeys::BindingResponse);
  ASSERT_TRUE(binding.ok());
  EXPECT_EQ(*binding, nullptr);

  // A plain string, as Golang's SetFilterState leaves one, reads as the factory builds it.
  filter_state.setData(FilterStateKeys::BindingResponse,
                       std::make_shared<Router::StringAccessorImpl>("/v1beta/models/{model}"),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  binding = LlmBindingObject::fromFilterState(filter_state, FilterStateKeys::BindingResponse);
  ASSERT_TRUE(binding.ok());
  EXPECT_EQ((*binding)->pattern()->source(), "/v1beta/models/{model}");

  // The binding of the object as set, or its error.
  auto set = std::make_shared<LlmBindingObject>("/v1/messages");
  filter_state.setData(FilterStateKeys::BindingRequest, set,
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  binding = LlmBindingObject::fromFilterState(filter_state, FilterStateKeys::BindingRequest);
  ASSERT_TRUE(binding.ok());
  EXPECT_EQ(*binding, *set->binding());
  filter_state.setData(FilterStateKeys::BindingRequest,
                       std::make_shared<LlmBindingObject>("/v1/{model"),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(
      LlmBindingObject::fromFilterState(filter_state, FilterStateKeys::BindingRequest).ok());

  // Any other object is not a binding.
  filter_state.setData(FilterStateKeys::LlmProtocolRequest,
                       std::make_shared<RequestLlmProtocol>(LLMProtocol::AnthropicMessages),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(
      LlmBindingObject::fromFilterState(filter_state, FilterStateKeys::LlmProtocolRequest).ok());
}

// Field access reads what a request leg read from the client's path.
TEST(LlmBindingReadObjectTest, ExposesTheReadByVariableName) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(LlmBindingReadObject::fromFilterState(filter_state), nullptr);

  auto object = std::make_shared<LlmBindingReadObject>(
      LlmBindingRead{nullptr, "claude-sonnet-4-5", {{"version", "v1"}, {"project", "acme"}}, {}});
  EXPECT_TRUE(object->hasFieldSupport());
  EXPECT_EQ(absl::get<absl::string_view>(object->getField("model")), "claude-sonnet-4-5");
  EXPECT_EQ(absl::get<absl::string_view>(object->getField("project")), "acme");
  EXPECT_EQ(absl::get<absl::string_view>(object->getField("version")), "v1");
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(object->getField("location")));

  filter_state.setData(FilterStateKeys::BindingRead, object,
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(LlmBindingReadObject::fromFilterState(filter_state), &object->read());

  const LlmBindingReadObject no_model(LlmBindingRead{});
  EXPECT_TRUE(absl::holds_alternative<absl::monostate>(no_model.getField("model")));
}

TEST(StringFromFilterStateTest, ReadsStringAccessorAtKey) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(stringFromFilterState(filter_state, FilterStateKeys::ModelResolved).has_value());

  const auto* factory =
      Registry::FactoryRegistry<StreamInfo::FilterState::ObjectFactory>::getFactory(
          FilterStateKeys::ModelResolved);
  ASSERT_NE(factory, nullptr);
  filter_state.setData(FilterStateKeys::ModelResolved,
                       std::shared_ptr<StreamInfo::FilterState::Object>(
                           factory->createFromBytes("claude-sonnet-4-5")),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_EQ(stringFromFilterState(filter_state, FilterStateKeys::ModelResolved),
            "claude-sonnet-4-5");
  // A non-string object at the key does not read as a string.
  filter_state.setData(FilterStateKeys::LlmProtocolResponse,
                       std::make_shared<ResponseLlmProtocol>(LLMProtocol::AnthropicMessages),
                       StreamInfo::FilterState::LifeSpan::FilterChain);
  EXPECT_FALSE(
      stringFromFilterState(filter_state, FilterStateKeys::LlmProtocolResponse).has_value());
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
