#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/filters/common/set_filter_state/filter_config.h"
#include "source/server/generic_factory_context.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace SetFilterState {
namespace {

class ObjectBarFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "bar"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

class ObjectFooFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return "foo"; }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    if (data == "BAD_VALUE") {
      return nullptr;
    }
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(ObjectBarFactory, StreamInfo::FilterState::ObjectFactory);
REGISTER_FACTORY(ObjectFooFactory, StreamInfo::FilterState::ObjectFactory);

class ConfigTest : public testing::Test {
public:
  void initialize(const std::vector<std::string>& values,
                  LifeSpan life_span = LifeSpan::FilterChain) {
    std::vector<FilterStateValueProto> proto_values;
    proto_values.reserve(values.size());
    for (const auto& value : values) {
      FilterStateValueProto proto_value;
      TestUtility::loadFromYaml(value, proto_value);
      proto_values.push_back(proto_value);
    }
    config_ = std::make_shared<Config>(
        Protobuf::RepeatedPtrField<FilterStateValueProto>(proto_values.begin(), proto_values.end()),
        life_span, context_);
  }
  void update() { config_->updateFilterState({&header_map_}, info_); }
  NiceMock<Server::Configuration::MockGenericFactoryContext> context_;
  Http::TestRequestHeaderMapImpl header_map_{{"test-header", "test-value"}};
  NiceMock<StreamInfo::MockStreamInfo> info_;
  ConfigSharedPtr config_;
};

TEST_F(ConfigTest, SetValue) {
  initialize({R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: "XXX"
  )YAML"});
  update();
  EXPECT_FALSE(info_.filterState()->hasDataAtOrAboveLifeSpan(LifeSpan::Request));
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  ASSERT_NE(nullptr, foo);
  EXPECT_EQ(foo->serializeAsString(), "XXX");
  EXPECT_EQ(0, info_.filterState()->objectsSharedWithUpstreamConnection()->size());
}

TEST_F(ConfigTest, SetValueConnection) {
  initialize({R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: "XXX"
  )YAML"},
             LifeSpan::Connection);
  update();
  EXPECT_TRUE(info_.filterState()->hasDataAtOrAboveLifeSpan(LifeSpan::Request));
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  ASSERT_NE(nullptr, foo);
  EXPECT_EQ(foo->serializeAsString(), "XXX");
  EXPECT_EQ(0, info_.filterState()->objectsSharedWithUpstreamConnection()->size());
}

TEST_F(ConfigTest, UpdateValue) {
  initialize({R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: "XXX"
  )YAML"});
  info_.filterState()->setData("foo", std::make_unique<Router::StringAccessorImpl>("OLD"),
                               StateType::Mutable);
  update();
  EXPECT_FALSE(info_.filterState()->hasDataAtOrAboveLifeSpan(LifeSpan::Request));
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  ASSERT_NE(nullptr, foo);
  EXPECT_EQ(foo->serializeAsString(), "XXX");
  EXPECT_EQ(0, info_.filterState()->objectsSharedWithUpstreamConnection()->size());
}

TEST_F(ConfigTest, SetValueFromHeader) {
  initialize({R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: "%REQ(test-header)%"
  )YAML"});
  update();
  EXPECT_FALSE(info_.filterState()->hasDataAtOrAboveLifeSpan(LifeSpan::Request));
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  ASSERT_NE(nullptr, foo);
  EXPECT_EQ(foo->serializeAsString(), "test-value");
  EXPECT_EQ(0, info_.filterState()->objectsSharedWithUpstreamConnection()->size());
}

TEST_F(ConfigTest, MultipleValues) {
  initialize({R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: "XXX"
  )YAML",
              R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: "YYY"
  )YAML",
              R"YAML(
    object_key: bar
    format_string:
      text_format_source:
        inline_string: "ZZZ"
  )YAML"});
  update();
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  ASSERT_NE(nullptr, foo);
  const auto* bar = info_.filterState()->getDataReadOnly<Router::StringAccessor>("bar");
  ASSERT_NE(nullptr, bar);
  EXPECT_EQ(foo->serializeAsString(), "YYY");
  EXPECT_EQ(bar->serializeAsString(), "ZZZ");
  EXPECT_EQ(0, info_.filterState()->objectsSharedWithUpstreamConnection()->size());
}

TEST_F(ConfigTest, BadValue) {
  initialize({R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: "BAD_VALUE"
  )YAML"});
  update();
  EXPECT_FALSE(info_.filterState()->hasDataAtOrAboveLifeSpan(LifeSpan::Request));
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  EXPECT_EQ(nullptr, foo);
}

TEST_F(ConfigTest, MissingKey) {
  EXPECT_THROW_WITH_MESSAGE(initialize({R"YAML(
    object_key: unknown_key
    format_string:
      text_format_source:
        inline_string: "XXX"
  )YAML"}),
                            EnvoyException, "'unknown_key' does not have an object factory");
}

TEST_F(ConfigTest, EmptyValue) {
  initialize({R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: ""
  )YAML"});
  update();
  EXPECT_FALSE(info_.filterState()->hasDataAtOrAboveLifeSpan(LifeSpan::Request));
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  ASSERT_NE(nullptr, foo);
  EXPECT_EQ(foo->serializeAsString(), "");
  EXPECT_EQ(0, info_.filterState()->objectsSharedWithUpstreamConnection()->size());
}

TEST_F(ConfigTest, EmptyValueSkip) {
  initialize({R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: ""
    skip_if_empty: true
  )YAML"});
  update();
  EXPECT_FALSE(info_.filterState()->hasDataAtOrAboveLifeSpan(LifeSpan::Request));
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  EXPECT_EQ(nullptr, foo);
}

TEST_F(ConfigTest, SetValueUpstreamSharedOnce) {
  initialize({R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: "XXX"
    shared_with_upstream: ONCE
  )YAML"});
  update();
  EXPECT_FALSE(info_.filterState()->hasDataAtOrAboveLifeSpan(LifeSpan::Request));
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  ASSERT_NE(nullptr, foo);
  EXPECT_EQ(foo->serializeAsString(), "XXX");
  const auto objects = info_.filterState()->objectsSharedWithUpstreamConnection();
  EXPECT_EQ(1, objects->size());
  EXPECT_EQ(StreamSharing::None, objects->at(0).stream_sharing_);
  EXPECT_EQ(StateType::Mutable, objects->at(0).state_type_);
  EXPECT_EQ("foo", objects->at(0).name_);
  EXPECT_EQ(foo, objects->at(0).data_.get());
}

TEST_F(ConfigTest, SetValueUpstreamSharedTransitive) {
  initialize({R"YAML(
    object_key: foo
    format_string:
      text_format_source:
        inline_string: "XXX"
    shared_with_upstream: TRANSITIVE
    read_only: true
  )YAML"});
  update();
  EXPECT_FALSE(info_.filterState()->hasDataAtOrAboveLifeSpan(LifeSpan::Request));
  const auto* foo = info_.filterState()->getDataReadOnly<Router::StringAccessor>("foo");
  ASSERT_NE(nullptr, foo);
  EXPECT_EQ(foo->serializeAsString(), "XXX");
  const auto objects = info_.filterState()->objectsSharedWithUpstreamConnection();
  EXPECT_EQ(1, objects->size());
  EXPECT_EQ(StreamSharing::SharedWithUpstreamConnection, objects->at(0).stream_sharing_);
  EXPECT_EQ(StateType::ReadOnly, objects->at(0).state_type_);
  EXPECT_EQ("foo", objects->at(0).name_);
  EXPECT_EQ(foo, objects->at(0).data_.get());
}

} // namespace
} // namespace SetFilterState
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
