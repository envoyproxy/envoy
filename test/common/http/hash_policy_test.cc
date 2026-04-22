#include "envoy/common/hashable.h"

#include "source/common/http/hash_policy.h"
#include "source/common/network/address_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

class HashPolicyImplTest : public testing::Test {
public:
  HashPolicyImplTest() : regex_engine_(std::make_shared<Envoy::Regex::GoogleReEngine>()) {}

  // Utility method to create a HashPolicyImpl based on a provided hash policy
  void setupHashPolicy(const envoy::config::route::v3::RouteAction::HashPolicy& policy) {
    hash_policy_impl_ = *HashPolicyImpl::create({&policy}, *regex_engine_);
  }

protected:
  Envoy::Regex::EnginePtr regex_engine_;
  TestRequestHeaderMapImpl headers_;
  Network::Address::Ipv4Instance local_address_{"127.0.0.1"};
  std::unique_ptr<HashPolicyImpl> hash_policy_impl_;
};

class HashableObj : public StreamInfo::FilterState::Object, public Hashable {
  absl::optional<uint64_t> hash() const override { return 1234567; };
};

// HeaderHashMethod: Verify hash is calculated correctly for a single header value
TEST_F(HashPolicyImplTest, HeaderHashForSingleHeaderValue) {
  envoy::config::route::v3::RouteAction::HashPolicy header_policy;
  header_policy.mutable_header()->set_header_name("x-test-header");

  setupHashPolicy(header_policy);

  headers_.addCopy("x-test-header", "test-value");

  absl::optional<uint64_t> hash = hash_policy_impl_->generateHash(headers_, {}, {});
  ASSERT_TRUE(hash.has_value());
  EXPECT_EQ(hash.value(), HashUtil::xxHash64("test-value"));
}

// HeaderHashMethod: Multiple header values for a single header.
TEST_F(HashPolicyImplTest, MultipleHeaderValues) {
  envoy::config::route::v3::RouteAction::HashPolicy header_policy;
  header_policy.mutable_header()->set_header_name("x-multi-header");

  setupHashPolicy(header_policy);

  headers_.addCopy("x-multi-header", "value1");
  headers_.addCopy("x-multi-header", "value2");

  absl::optional<uint64_t> hash = hash_policy_impl_->generateHash(headers_, {}, {});
  ASSERT_TRUE(hash.has_value());

  absl::InlinedVector<absl::string_view, 1> sorted_header_values = {"value1", "value2"};
  uint64_t expected_hash = HashUtil::xxHash64(absl::MakeSpan(sorted_header_values));
  EXPECT_EQ(hash.value(), expected_hash);
}

// HeaderHashMethod: Same hash for different order of header values.
TEST_F(HashPolicyImplTest, HeaderHashMethodSameHashForDifferentHeaderOrder) {
  envoy::config::route::v3::RouteAction::HashPolicy header_policy;
  header_policy.mutable_header()->set_header_name("x-reorder-header");

  setupHashPolicy(header_policy);

  headers_.addCopy("x-reorder-header", "value1");
  headers_.addCopy("x-reorder-header", "value2");

  absl::optional<uint64_t> hash1 = hash_policy_impl_->generateHash(headers_, {}, {});
  ASSERT_TRUE(hash1.has_value());

  headers_.remove("x-reorder-header");
  headers_.addCopy("x-reorder-header", "value2");
  headers_.addCopy("x-reorder-header", "value1");

  absl::optional<uint64_t> hash2 = hash_policy_impl_->generateHash(headers_, {}, {});
  ASSERT_TRUE(hash2.has_value());

  EXPECT_EQ(hash1.value(), hash2.value());
}

// HeaderHashMethod: Header is not present in the request headers.
TEST_F(HashPolicyImplTest, HeaderNotPresent) {
  envoy::config::route::v3::RouteAction::HashPolicy header_policy;
  header_policy.mutable_header()->set_header_name("x-missing-header");

  setupHashPolicy(header_policy);

  {
    absl::optional<uint64_t> hash = hash_policy_impl_->generateHash({}, {}, {});
    EXPECT_FALSE(hash.has_value());
  }
  {
    absl::optional<uint64_t> hash = hash_policy_impl_->generateHash(headers_, {}, {});
    EXPECT_FALSE(hash.has_value());
  }
}

// HeaderHashMethod: Regex rewrite pattern applied to header value.
TEST_F(HashPolicyImplTest, RegexRewriteApplied) {
  envoy::config::route::v3::RouteAction::HashPolicy header_policy;
  header_policy.mutable_header()->set_header_name("x-test-header");

  // Set regex rewrite pattern and substitution.
  auto* regex_spec = header_policy.mutable_header()->mutable_regex_rewrite();
  regex_spec->set_substitution("replaced");
  auto* pattern = regex_spec->mutable_pattern();
  pattern->mutable_google_re2();
  pattern->set_regex("test");

  setupHashPolicy(header_policy);

  headers_.addCopy("x-test-header", "test-value");

  absl::optional<uint64_t> hash = hash_policy_impl_->generateHash(headers_, {}, {});
  ASSERT_TRUE(hash.has_value());
  EXPECT_EQ(hash.value(), HashUtil::xxHash64("replaced-value"));
}

// CookieHashMethod: Verifies that the hash is calculated correctly when a specified cookie is
// present.
TEST_F(HashPolicyImplTest, CookieHashForPresentCookie) {
  envoy::config::route::v3::RouteAction::HashPolicy cookie_policy;
  cookie_policy.mutable_cookie()->set_name("test-cookie");

  setupHashPolicy(cookie_policy);

  headers_.setCopy(Http::LowerCaseString("cookie"), "test-cookie=cookie-value");

  absl::optional<uint64_t> hash = hash_policy_impl_->generateHash(headers_, {}, {});
  ASSERT_TRUE(hash.has_value());
  EXPECT_EQ(hash.value(), HashUtil::xxHash64("cookie-value"));
}

// CookieHashMethod: Verifies that a new cookie is set and the hash is calculated correctly when the
// cookie is absent but TTL is defined.
TEST_F(HashPolicyImplTest, CookieHashForAbsentCookieWithTTL) {
  envoy::config::route::v3::RouteAction::HashPolicy cookie_policy;
  cookie_policy.mutable_cookie()->set_name("test-cookie");
  cookie_policy.mutable_cookie()->mutable_ttl()->set_seconds(60);

  setupHashPolicy(cookie_policy);

  // Simulate the callback used for adding a new cookie when TTL is defined
  Http::HashPolicy::AddCookieCallback add_cookie =
      [](absl::string_view, absl::string_view, std::chrono::seconds,
         absl::Span<const CookieAttribute>) -> std::string { return "new-cookie-value"; };

  absl::optional<uint64_t> hash = hash_policy_impl_->generateHash(headers_, {}, add_cookie);
  ASSERT_TRUE(hash.has_value());
  EXPECT_EQ(hash.value(), HashUtil::xxHash64("new-cookie-value"));
}

// CookieHashMethod: Verifies that no hash is generated when the cookie is absent and no TTL is
// defined.
TEST_F(HashPolicyImplTest, CookieHashForAbsentCookieWithoutTTL) {
  envoy::config::route::v3::RouteAction::HashPolicy cookie_policy;
  cookie_policy.mutable_cookie()->set_name("test-cookie");

  setupHashPolicy(cookie_policy);

  absl::optional<uint64_t> hash = hash_policy_impl_->generateHash(headers_, {}, {});
  ASSERT_FALSE(hash.has_value());
}

// Test IpHashMethod for valid ip
TEST_F(HashPolicyImplTest, IpHashForValidIp) {
  envoy::config::route::v3::RouteAction::HashPolicy ip_policy;
  ip_policy.mutable_connection_properties()->set_source_ip(true);

  setupHashPolicy(ip_policy);

  auto downstream_address = std::make_shared<Network::Address::Ipv4Instance>("192.168.1.1");

  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setRemoteAddress(downstream_address);

  absl::optional<uint64_t> hash = hash_policy_impl_->generateHash(headers_, stream_info, nullptr);
  ASSERT_TRUE(hash.has_value());
  EXPECT_EQ(hash.value(), HashUtil::xxHash64("192.168.1.1"));
}

// Test IpHashMethod with nullptr address
TEST_F(HashPolicyImplTest, IpHashForNullAddress) {
  envoy::config::route::v3::RouteAction::HashPolicy ip_policy;
  ip_policy.mutable_connection_properties()->set_source_ip(true);

  setupHashPolicy(ip_policy);
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.downstream_connection_info_provider_->setRemoteAddress(nullptr);

  ASSERT_FALSE(hash_policy_impl_->generateHash(headers_, stream_info, nullptr).has_value());
}

// Test QueryParameterHashMethod to verify hash generation for a query parameter
TEST_F(HashPolicyImplTest, QueryParameterHashForExistingParameter) {
  envoy::config::route::v3::RouteAction::HashPolicy query_param_policy;
  query_param_policy.mutable_query_parameter()->set_name("test-param");

  setupHashPolicy(query_param_policy);

  headers_.setPath("/test?test-param=param-value");

  absl::optional<uint64_t> hash = hash_policy_impl_->generateHash(headers_, {}, {});
  ASSERT_TRUE(hash.has_value());
  EXPECT_EQ(hash.value(), HashUtil::xxHash64("param-value"));
}

// Test QueryParameterHashMethod when the parameter is absent in the query string
TEST_F(HashPolicyImplTest, QueryParameterHashForAbsentParameter) {
  envoy::config::route::v3::RouteAction::HashPolicy query_param_policy;
  query_param_policy.mutable_query_parameter()->set_name("missing-param");

  setupHashPolicy(query_param_policy);

  // Set up the path header without the target query parameter
  headers_.setPath("/test?some-param=other-value");

  ASSERT_FALSE(hash_policy_impl_->generateHash(headers_, {}, {}).has_value());
}

// Test QueryParameterHashMethod when the path header is absent
TEST_F(HashPolicyImplTest, QueryParameterHashForMissingPathHeader) {
  envoy::config::route::v3::RouteAction::HashPolicy query_param_policy;
  query_param_policy.mutable_query_parameter()->set_name("test-param");

  setupHashPolicy(query_param_policy);

  ASSERT_FALSE(hash_policy_impl_->generateHash(headers_, {}, {}).has_value());
}

// Test FilterStateHashMethod when the filter state has the expected key.
TEST_F(HashPolicyImplTest, FilterStateHashForExistingKey) {
  envoy::config::route::v3::RouteAction::HashPolicy filter_state_policy;
  filter_state_policy.mutable_filter_state()->set_key("test-key");

  setupHashPolicy(filter_state_policy);

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Request);
  filter_state->setData("test-key", std::make_unique<HashableObj>(),
                        StreamInfo::FilterState::StateType::ReadOnly);
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.filter_state_ = filter_state;

  absl::optional<uint64_t> hash = hash_policy_impl_->generateHash(headers_, stream_info, {});
  ASSERT_TRUE(hash.has_value());
  EXPECT_EQ(hash.value(), 1234567UL);
}

// Test FilterStateHashMethod when the filter state key is absent.
TEST_F(HashPolicyImplTest, FilterStateHashForAbsentKey) {
  envoy::config::route::v3::RouteAction::HashPolicy filter_state_policy;
  filter_state_policy.mutable_filter_state()->set_key("missing-key");

  setupHashPolicy(filter_state_policy);

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Request);
  filter_state->setData("another-key", std::make_unique<HashableObj>(),
                        StreamInfo::FilterState::StateType::ReadOnly);
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.filter_state_ = filter_state;

  ASSERT_FALSE(hash_policy_impl_->generateHash(headers_, stream_info, {})
                   .has_value()); // Expecting no hash generated
}

// Test FilterStateHashMethod when key has no value
TEST_F(HashPolicyImplTest, FilterStateHashForNullFilterState) {
  envoy::config::route::v3::RouteAction::HashPolicy filter_state_policy;
  filter_state_policy.mutable_filter_state()->set_key("test-key");

  setupHashPolicy(filter_state_policy);

  auto filter_state =
      std::make_shared<StreamInfo::FilterStateImpl>(StreamInfo::FilterState::LifeSpan::Request);
  filter_state->setData("test-key", nullptr, StreamInfo::FilterState::StateType::ReadOnly);
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.filter_state_ = filter_state;

  ASSERT_FALSE(hash_policy_impl_->generateHash(headers_, stream_info, {}).has_value());
}

} // namespace Http
} // namespace Envoy
