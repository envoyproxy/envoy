#include "source/common/router/string_accessor_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/http/early_header_mutation/dynamic_modules/early_header_mutation.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace EarlyHeaderMutation {
namespace DynamicModules {
namespace {

envoy_dynamic_module_type_module_buffer strBuf(absl::string_view s) { return {s.data(), s.size()}; }

// Exercises the extern "C" callbacks directly against a stack-allocated context, without loading a
// module. This is the only place the callback bodies are covered in isolation.
class EarlyHeaderMutationAbiImplTest : public testing::Test {
public:
  EarlyHeaderMutationAbiImplTest() : context_{headers_, stream_info_} {}

  envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr ptr() {
    return static_cast<void*>(&context_);
  }

  Envoy::Http::TestRequestHeaderMapImpl headers_{
      {":method", "GET"}, {"x-single", "a"}, {"x-multi", "v1"}, {"x-multi", "v2"}};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  EarlyHeaderMutationContext context_;
};

TEST_F(EarlyHeaderMutationAbiImplTest, GetHeadersSize) {
  EXPECT_EQ(headers_.size(),
            envoy_dynamic_module_callback_early_header_mutation_get_headers_size(ptr()));
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetHeadersPopulatesArray) {
  const size_t count = envoy_dynamic_module_callback_early_header_mutation_get_headers_size(ptr());
  std::vector<envoy_dynamic_module_type_envoy_http_header> result(count);
  ASSERT_TRUE(
      envoy_dynamic_module_callback_early_header_mutation_get_headers(ptr(), result.data()));
  bool found_single = false;
  for (const auto& header : result) {
    if (absl::string_view(header.key_ptr, header.key_length) == "x-single") {
      EXPECT_EQ("a", absl::string_view(header.value_ptr, header.value_length));
      found_single = true;
    }
  }
  EXPECT_TRUE(found_single);
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetHeaderValueSingle) {
  envoy_dynamic_module_type_envoy_buffer value{nullptr, 0};
  size_t total = 0;
  ASSERT_TRUE(envoy_dynamic_module_callback_early_header_mutation_get_header_value(
      ptr(), strBuf("x-single"), &value, 0, &total));
  EXPECT_EQ("a", absl::string_view(value.ptr, value.length));
  EXPECT_EQ(1, total);
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetHeaderValueMultiValueByIndex) {
  envoy_dynamic_module_type_envoy_buffer value{nullptr, 0};
  size_t total = 0;
  ASSERT_TRUE(envoy_dynamic_module_callback_early_header_mutation_get_header_value(
      ptr(), strBuf("x-multi"), &value, 1, &total));
  EXPECT_EQ("v2", absl::string_view(value.ptr, value.length));
  EXPECT_EQ(2, total);
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetHeaderValueMissingKey) {
  envoy_dynamic_module_type_envoy_buffer value{reinterpret_cast<char*>(1), 1};
  size_t total = 1;
  EXPECT_FALSE(envoy_dynamic_module_callback_early_header_mutation_get_header_value(
      ptr(), strBuf("x-absent"), &value, 0, &total));
  EXPECT_EQ(nullptr, value.ptr);
  EXPECT_EQ(0, value.length);
  EXPECT_EQ(0, total);
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetHeaderValueIndexOutOfRange) {
  envoy_dynamic_module_type_envoy_buffer value{nullptr, 0};
  size_t total = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_early_header_mutation_get_header_value(
      ptr(), strBuf("x-multi"), &value, 5, &total));
  // The total count is still reported so the module can bound its own iteration.
  EXPECT_EQ(2, total);
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetHeaderValueNullTotalCountOut) {
  envoy_dynamic_module_type_envoy_buffer value{nullptr, 0};
  EXPECT_TRUE(envoy_dynamic_module_callback_early_header_mutation_get_header_value(
      ptr(), strBuf("x-single"), &value, 0, nullptr));
}

TEST_F(EarlyHeaderMutationAbiImplTest, SetHeaderCreates) {
  EXPECT_TRUE(envoy_dynamic_module_callback_early_header_mutation_set_header(ptr(), strBuf("x-new"),
                                                                             strBuf("value")));
  EXPECT_EQ("value", headers_.get_("x-new"));
}

TEST_F(EarlyHeaderMutationAbiImplTest, SetHeaderReplacesAllValues) {
  EXPECT_TRUE(envoy_dynamic_module_callback_early_header_mutation_set_header(
      ptr(), strBuf("x-multi"), strBuf("only")));
  EXPECT_EQ(1, headers_.get(Envoy::Http::LowerCaseString("x-multi")).size());
  EXPECT_EQ("only", headers_.get_("x-multi"));
}

TEST_F(EarlyHeaderMutationAbiImplTest, SetHeaderWithEmptyValueSetsEmpty) {
  EXPECT_TRUE(envoy_dynamic_module_callback_early_header_mutation_set_header(
      ptr(), strBuf("x-single"), {nullptr, 0}));
  EXPECT_EQ(1, headers_.get(Envoy::Http::LowerCaseString("x-single")).size());
  EXPECT_EQ("", headers_.get_("x-single"));
}

TEST_F(EarlyHeaderMutationAbiImplTest, SetHeaderUppercaseKeyIsLowercased) {
  EXPECT_TRUE(envoy_dynamic_module_callback_early_header_mutation_set_header(
      ptr(), strBuf("X-Mixed-Case"), strBuf("v")));
  EXPECT_EQ("v", headers_.get_("x-mixed-case"));
}

TEST_F(EarlyHeaderMutationAbiImplTest, SetHeaderEmptyKeyRejected) {
  EXPECT_FALSE(envoy_dynamic_module_callback_early_header_mutation_set_header(ptr(), {nullptr, 0},
                                                                              strBuf("v")));
}

TEST_F(EarlyHeaderMutationAbiImplTest, AddHeaderPreservesExisting) {
  EXPECT_TRUE(envoy_dynamic_module_callback_early_header_mutation_add_header(
      ptr(), strBuf("x-single"), strBuf("b")));
  EXPECT_EQ(2, headers_.get(Envoy::Http::LowerCaseString("x-single")).size());
}

TEST_F(EarlyHeaderMutationAbiImplTest, AddHeaderCreatesWhenAbsent) {
  EXPECT_TRUE(envoy_dynamic_module_callback_early_header_mutation_add_header(
      ptr(), strBuf("x-fresh"), strBuf("v")));
  EXPECT_EQ("v", headers_.get_("x-fresh"));
}

TEST_F(EarlyHeaderMutationAbiImplTest, AddHeaderEmptyKeyRejected) {
  EXPECT_FALSE(envoy_dynamic_module_callback_early_header_mutation_add_header(ptr(), {nullptr, 0},
                                                                              strBuf("v")));
}

TEST_F(EarlyHeaderMutationAbiImplTest, RemoveHeaderExisting) {
  EXPECT_TRUE(
      envoy_dynamic_module_callback_early_header_mutation_remove_header(ptr(), strBuf("x-multi")));
  EXPECT_TRUE(headers_.get(Envoy::Http::LowerCaseString("x-multi")).empty());
}

TEST_F(EarlyHeaderMutationAbiImplTest, RemoveHeaderMissingIsSuccess) {
  EXPECT_TRUE(
      envoy_dynamic_module_callback_early_header_mutation_remove_header(ptr(), strBuf("x-absent")));
}

TEST_F(EarlyHeaderMutationAbiImplTest, RemoveHeaderEmptyKeyRejected) {
  EXPECT_FALSE(
      envoy_dynamic_module_callback_early_header_mutation_remove_header(ptr(), {nullptr, 0}));
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetAttributeStringRequestProtocol) {
  stream_info_.protocol_ = Envoy::Http::Protocol::Http11;
  envoy_dynamic_module_type_envoy_buffer value{nullptr, 0};
  ASSERT_TRUE(envoy_dynamic_module_callback_early_header_mutation_get_attribute_string(
      ptr(), envoy_dynamic_module_type_attribute_id_RequestProtocol, &value));
  EXPECT_EQ("HTTP/1.1", absl::string_view(value.ptr, value.length));
}

// An attribute that is not populated must report absence rather than a stale or default value.
TEST_F(EarlyHeaderMutationAbiImplTest, GetAttributeStringUnsetProtocol) {
  stream_info_.protocol_ = std::nullopt;
  envoy_dynamic_module_type_envoy_buffer value{nullptr, 0};
  EXPECT_FALSE(envoy_dynamic_module_callback_early_header_mutation_get_attribute_string(
      ptr(), envoy_dynamic_module_type_attribute_id_RequestProtocol, &value));
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetAttributeIntConnectionId) {
  uint64_t value = 0;
  EXPECT_TRUE(envoy_dynamic_module_callback_early_header_mutation_get_attribute_int(
      ptr(), envoy_dynamic_module_type_attribute_id_ConnectionId, &value));
}

// Without a TLS connection the mTLS attribute is unavailable, so the getter reports absence.
TEST_F(EarlyHeaderMutationAbiImplTest, GetAttributeBoolConnectionMtlsUnavailable) {
  bool value = true;
  EXPECT_FALSE(envoy_dynamic_module_callback_early_header_mutation_get_attribute_bool(
      ptr(), envoy_dynamic_module_type_attribute_id_ConnectionMtls, &value));
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetDynamicMetadataString) {
  Protobuf::Struct metadata;
  (*metadata.mutable_fields())["key"].set_string_value("value");
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.test"] = metadata;

  envoy_dynamic_module_type_envoy_buffer value{nullptr, 0};
  ASSERT_TRUE(envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata(
      ptr(), strBuf("envoy.test"), strBuf("key"), &value));
  EXPECT_EQ("value", absl::string_view(value.ptr, value.length));

  // Absent namespace and wrong-type reads both fail rather than returning a stale value.
  EXPECT_FALSE(envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata(
      ptr(), strBuf("envoy.absent"), strBuf("key"), &value));
  double number = 0;
  EXPECT_FALSE(envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_number(
      ptr(), strBuf("envoy.test"), strBuf("key"), &number));
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetDynamicMetadataNumberAndBool) {
  Protobuf::Struct metadata;
  (*metadata.mutable_fields())["number"].set_number_value(42);
  (*metadata.mutable_fields())["flag"].set_bool_value(true);
  (*stream_info_.metadata_.mutable_filter_metadata())["envoy.test"] = metadata;

  double number = 0;
  ASSERT_TRUE(envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_number(
      ptr(), strBuf("envoy.test"), strBuf("number"), &number));
  EXPECT_EQ(42, number);

  bool flag = false;
  ASSERT_TRUE(envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_bool(
      ptr(), strBuf("envoy.test"), strBuf("flag"), &flag));
  EXPECT_TRUE(flag);
}

TEST_F(EarlyHeaderMutationAbiImplTest, GetFilterStateBytes) {
  stream_info_.filterState()->setData("envoy.test.state",
                                      std::make_shared<Router::StringAccessorImpl>("state-value"),
                                      StreamInfo::FilterState::LifeSpan::FilterChain);

  envoy_dynamic_module_type_envoy_buffer value{nullptr, 0};
  ASSERT_TRUE(envoy_dynamic_module_callback_early_header_mutation_get_filter_state_bytes(
      ptr(), strBuf("envoy.test.state"), &value));
  EXPECT_EQ("state-value", absl::string_view(value.ptr, value.length));

  EXPECT_FALSE(envoy_dynamic_module_callback_early_header_mutation_get_filter_state_bytes(
      ptr(), strBuf("envoy.absent"), &value));
  EXPECT_FALSE(envoy_dynamic_module_callback_early_header_mutation_get_filter_state_bytes(
      ptr(), strBuf("envoy.test.state"), nullptr));
}

} // namespace
} // namespace DynamicModules
} // namespace EarlyHeaderMutation
} // namespace Http
} // namespace Extensions
} // namespace Envoy
