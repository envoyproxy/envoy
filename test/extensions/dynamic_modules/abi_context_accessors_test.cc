#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/registry/registry.h"

#include "source/common/router/string_accessor_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/dynamic_modules/abi_context_accessors.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {
namespace {

using LifeSpan = StreamInfo::FilterState::LifeSpan;

constexpr absl::string_view Key = "envoy.test.context_accessor_object";

class TestObjectFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override { return std::string(Key); }
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override {
    return std::make_unique<Router::StringAccessorImpl>(data);
  }
};

REGISTER_FACTORY(TestObjectFactory, StreamInfo::FilterState::ObjectFactory);

enum class Setter { Bytes, Typed };

class FilterStateSetterTest : public testing::TestWithParam<Setter> {
protected:
  bool set(absl::string_view value, std::optional<LifeSpan> life_span = std::nullopt) {
    if (GetParam() == Setter::Bytes) {
      return ContextAccessor::setFilterStateBytes(stream_info_, Key, value, life_span);
    }
    return ContextAccessor::setFilterStateTyped(stream_info_, Key, value, life_span);
  }

  void expectRefused(std::optional<LifeSpan> life_span) {
    EXPECT_LOG_CONTAINS("debug", "is already set at a different life span",
                        EXPECT_FALSE(set("refused", life_span)));
  }

  std::string stored() const {
    const auto* accessor = stream_info_.filter_state_->getDataReadOnly<Router::StringAccessor>(Key);
    return accessor == nullptr ? "" : std::string(accessor->asString());
  }

  void useFilterStateWithParents(StreamInfo::FilterStateSharedPtr connection) {
    stream_info_.filter_state_ =
        std::make_shared<StreamInfo::FilterStateImpl>(std::move(connection), LifeSpan::FilterChain);
  }

  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

INSTANTIATE_TEST_SUITE_P(Setters, FilterStateSetterTest,
                         testing::Values(Setter::Bytes, Setter::Typed));

TEST_P(FilterStateSetterTest, StoresNewKey) {
  EXPECT_TRUE(set("value"));
  EXPECT_EQ("value", stored());

  for (const LifeSpan life_span : {LifeSpan::Request, LifeSpan::Connection}) {
    stream_info_.filter_state_ =
        std::make_shared<StreamInfo::FilterStateImpl>(LifeSpan::FilterChain);
    EXPECT_TRUE(set("value", life_span));
    EXPECT_EQ("value", stored());
    EXPECT_TRUE(stream_info_.filter_state_->hasDataAtOrAboveLifeSpan(life_span));
  }
}

TEST_P(FilterStateSetterTest, OverwritesKeyAtSameLifeSpan) {
  EXPECT_TRUE(set("first"));
  EXPECT_TRUE(set("second", LifeSpan::FilterChain));
  EXPECT_EQ("second", stored());

  for (const LifeSpan life_span : {LifeSpan::Request, LifeSpan::Connection}) {
    stream_info_.filter_state_ =
        std::make_shared<StreamInfo::FilterStateImpl>(LifeSpan::FilterChain);
    EXPECT_TRUE(set("first", life_span));
    EXPECT_TRUE(set("second", life_span));
    EXPECT_EQ("second", stored());
  }
}

TEST_P(FilterStateSetterTest, RefusesKeyHeldByParentAtRequest) {
  EXPECT_TRUE(set("request", LifeSpan::Request));
  expectRefused(std::nullopt);
  expectRefused(LifeSpan::FilterChain);
  expectRefused(LifeSpan::Connection);
  EXPECT_EQ("request", stored());
}

TEST_P(FilterStateSetterTest, RefusesKeyHeldByParentAtConnection) {
  EXPECT_TRUE(set("connection", LifeSpan::Connection));
  expectRefused(std::nullopt);
  expectRefused(LifeSpan::Request);
  EXPECT_EQ("connection", stored());
}

TEST_P(FilterStateSetterTest, RefusesKeyHeldAtShorterLifeSpan) {
  EXPECT_TRUE(set("chain"));
  expectRefused(LifeSpan::Request);
  expectRefused(LifeSpan::Connection);
  EXPECT_EQ("chain", stored());

  // The parent levels already exist, as for a downstream stream.
  useFilterStateWithParents(std::make_shared<StreamInfo::FilterStateImpl>(LifeSpan::Connection));
  EXPECT_TRUE(set("chain"));
  expectRefused(LifeSpan::Request);
  expectRefused(LifeSpan::Connection);
  EXPECT_EQ("chain", stored());
}

TEST_P(FilterStateSetterTest, OverwritesKeyHeldBySharedConnection) {
  auto connection = std::make_shared<StreamInfo::FilterStateImpl>(LifeSpan::Connection);
  connection->setData(Key, std::make_shared<Router::StringAccessorImpl>("connection"),
                      LifeSpan::Connection);
  useFilterStateWithParents(connection);
  EXPECT_TRUE(set("stream", LifeSpan::Connection));
  EXPECT_EQ("stream", stored());
}

TEST_P(FilterStateSetterTest, RefusesKeyHeldByStreamAndSharedConnection) {
  auto connection = std::make_shared<StreamInfo::FilterStateImpl>(LifeSpan::Connection);
  useFilterStateWithParents(connection);
  EXPECT_TRUE(set("stream"));
  // Another user of the connection, such as a network filter, stores the key there too.
  connection->setData(Key, std::make_shared<Router::StringAccessorImpl>("connection"),
                      LifeSpan::Connection);
  expectRefused(LifeSpan::Connection);
  expectRefused(LifeSpan::Request);
  expectRefused(std::nullopt);
  EXPECT_EQ("stream", stored());
}

TEST(FilterStateIndexedKeyTest, RefusesKeyHeldAtOtherLifeSpan) {
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const absl::string_view key =
      StreamInfo::FilterState::indexToName(StreamInfo::FilterStateIndex::UpstreamServerName);
  EXPECT_TRUE(ContextAccessor::setFilterStateBytes(stream_info, key, "request", LifeSpan::Request));
  EXPECT_LOG_CONTAINS(
      "debug", "is already set at a different life span",
      EXPECT_FALSE(ContextAccessor::setFilterStateBytes(stream_info, key, "chain")));
  EXPECT_TRUE(ContextAccessor::setFilterStateBytes(stream_info, key, "second", LifeSpan::Request));
  const auto* accessor = stream_info.filter_state_->getDataReadOnly<Router::StringAccessor>(key);
  ASSERT_NE(nullptr, accessor);
  EXPECT_EQ("second", accessor->asString());
}

envoy_dynamic_module_type_module_http_header moduleHeader(absl::string_view key,
                                                          absl::string_view value) {
  return {key.data(), key.size(), value.data(), value.size()};
}

TEST(ModuleHeaderKeyTest, RejectsWhatHttpDoesNotAllow) {
  for (const absl::string_view name :
       {absl::string_view(""), absl::string_view(":"), absl::string_view("bad name"),
        absl::string_view("a\rb"), absl::string_view("a\nb"), absl::string_view("a\0b", 3)}) {
    SCOPED_TRACE(testing::PrintToString(std::string(name)));
    EXPECT_FALSE(moduleHeaderKey(name).has_value());
  }
  for (const absl::string_view value :
       {absl::string_view("a\rb"), absl::string_view("a\nb"), absl::string_view("a\0b", 3),
        absl::string_view("bell\a")}) {
    SCOPED_TRACE(testing::PrintToString(std::string(value)));
    EXPECT_FALSE(moduleHeaderKey("x-key", value).has_value());
  }
}

TEST(ModuleHeaderKeyTest, AcceptsWhatHttpAllows) {
  EXPECT_EQ(moduleHeaderKey("X-Mixed-Case"), Http::LowerCaseString("x-mixed-case"));
  EXPECT_EQ(moduleHeaderKey(":path", "/a?b=c"), Http::LowerCaseString(":path"));
  EXPECT_EQ(moduleHeaderKey("x-key", "a\tb \x80\xff"), Http::LowerCaseString("x-key"));
  EXPECT_EQ(moduleHeaderKey("x-key", ""), Http::LowerCaseString("x-key"));
}

TEST(ModuleHeaderKeyTest, RuntimeGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.dynamic_modules_reject_invalid_headers", "false"}});
  // HTTP does not allow these bytes, but the header map's asserts do not catch them.
  EXPECT_EQ(moduleHeaderKey("Bad Name", "bell\a"), Http::LowerCaseString("bad name"));
}

TEST(AddModuleHeadersTest, SkipsInvalidHeaders) {
  Http::TestRequestHeaderMapImpl map;
  const std::vector<envoy_dynamic_module_type_module_http_header> headers = {
      moduleHeader("x-valid", "value"), moduleHeader("x-bad", "value\r\nx-injected: 1"),
      moduleHeader("", "value")};
  EXPECT_FALSE(addModuleHeaders(map, headers.data(), headers.size()));
  EXPECT_EQ(map.size(), 1);
  EXPECT_EQ(map.get_("x-valid"), "value");

  EXPECT_TRUE(addModuleHeaders(map, headers.data(), 1));
  EXPECT_EQ(map.get(Http::LowerCaseString("x-valid")).size(), 2);
  EXPECT_TRUE(addModuleHeaders(map, nullptr, 0));
}

} // namespace
} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
