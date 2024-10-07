#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.h"

#include "source/common/network/address_impl.h"
#include "source/extensions/http/original_ip_detection/xff/xff.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

class XffNumTrustedHopsTest : public testing::Test {
protected:
  XffNumTrustedHopsTest() {
    envoy::extensions::http::original_ip_detection::xff::v3::XffConfig config;
    config.set_xff_num_trusted_hops(1);
    xff_extension_ = *XffIPDetection::create(config);
  }

  std::shared_ptr<XffIPDetection> xff_extension_;
};

TEST_F(XffNumTrustedHopsTest, Detection) {
  // Header missing.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers{{"x-other", "abc"}};
    Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = xff_extension_->detect(params);

    EXPECT_EQ(nullptr, result.detected_remote_address);
    EXPECT_FALSE(result.allow_trusted_address_checks);
  }

  // Good request.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers{{"x-forwarded-for", "1.2.3.4,2.2.2.2"}};
    Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = xff_extension_->detect(params);

    EXPECT_EQ("1.2.3.4:0", result.detected_remote_address->asString());
    EXPECT_FALSE(result.allow_trusted_address_checks);
  }
}

class XffTrustedCidrsTest : public testing::Test {
protected:
  XffTrustedCidrsTest() {
    envoy::extensions::http::original_ip_detection::xff::v3::XffConfig config;
    auto cidr1 = config.mutable_xff_trusted_cidrs()->add_cidrs();
    cidr1->set_address_prefix("192.0.2.0");
    cidr1->mutable_prefix_len()->set_value(24);
    auto cidr2 = config.mutable_xff_trusted_cidrs()->add_cidrs();
    cidr2->set_address_prefix("198.51.100.0");
    cidr2->mutable_prefix_len()->set_value(24);
    auto cidr3 = config.mutable_xff_trusted_cidrs()->add_cidrs();
    cidr3->set_address_prefix("2001:db8:7e57:1::");
    cidr3->mutable_prefix_len()->set_value(64);
    config.mutable_skip_xff_append()->set_value(false);
    xff_extension_ = *XffIPDetection::create(config);
  }

  std::shared_ptr<XffIPDetection> xff_extension_;
};

TEST_F(XffTrustedCidrsTest, RemoteNotTrusted) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{"x-forwarded-for", "1.2.3.4,2.2.2.2"}};

  auto remote_address = std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11");
  Envoy::Http::OriginalIPDetectionParams params = {headers, remote_address};
  auto result = xff_extension_->detect(params);
  ASSERT_EQ(result.detected_remote_address, nullptr);
}

TEST_F(XffTrustedCidrsTest, RemoteIPv4IsTrusted) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{"x-forwarded-for", "1.2.3.4,2.2.2.2"}};

  auto remote_address = std::make_shared<Network::Address::Ipv4Instance>("192.0.2.11");
  Envoy::Http::OriginalIPDetectionParams params = {headers, remote_address};
  auto result = xff_extension_->detect(params);
  ASSERT_NE(result.detected_remote_address, nullptr);
  EXPECT_EQ("2.2.2.2", result.detected_remote_address->ip()->addressAsString());
}

TEST_F(XffTrustedCidrsTest, RemoteIPv6IsTrusted) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{"x-forwarded-for", "1.2.3.4,2.2.2.2"}};

  auto remote_address = std::make_shared<Network::Address::Ipv6Instance>("2001:db8:7e57:1::1");
  Envoy::Http::OriginalIPDetectionParams params = {headers, remote_address};
  auto result = xff_extension_->detect(params);
  ASSERT_NE(result.detected_remote_address, nullptr);
  EXPECT_EQ("2.2.2.2", result.detected_remote_address->ip()->addressAsString());
}

TEST_F(XffTrustedCidrsTest, Recurse) {
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {"x-forwarded-for", "1.2.3.4,2.2.2.2,192.0.2.5, 198.51.100.1"}};

  auto remote_address = std::make_shared<Network::Address::Ipv4Instance>("192.0.2.11");
  Envoy::Http::OriginalIPDetectionParams params = {headers, remote_address};
  auto result = xff_extension_->detect(params);
  ASSERT_NE(result.detected_remote_address, nullptr);
  EXPECT_EQ("2.2.2.2", result.detected_remote_address->ip()->addressAsString());
}

TEST_F(XffTrustedCidrsTest, RecurseAllAddressesTrusted) {
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {"x-forwarded-for", "192.0.2.4,192.0.2.5, 198.51.100.1"}};

  auto remote_address = std::make_shared<Network::Address::Ipv4Instance>("192.0.2.11");
  Envoy::Http::OriginalIPDetectionParams params = {headers, remote_address};
  auto result = xff_extension_->detect(params);
  ASSERT_NE(result.detected_remote_address, nullptr);
  EXPECT_EQ("192.0.2.4", result.detected_remote_address->ip()->addressAsString());
}

TEST_F(XffTrustedCidrsTest, EmptyXFFEntry) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{"x-forwarded-for", "192.0.2.4,, 198.51.100.1"}};

  auto remote_address = std::make_shared<Network::Address::Ipv4Instance>("192.0.2.11");
  Envoy::Http::OriginalIPDetectionParams params = {headers, remote_address};
  auto result = xff_extension_->detect(params);
  ASSERT_EQ(result.detected_remote_address, nullptr);
}

TEST_F(XffTrustedCidrsTest, XFFHasTooManyEntries) {
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {"x-forwarded-for",
       "10.1.1.1, 10.2.2.2, 10.3.3.3, 10.4.4.4, 10.5.5.5, 10.6.6.6, 10.7.7.7,10.8.8.8,"
       "10.9.9.9, 10.10.10.10, 10.11.11.11, 10.12.12.12, 10.13.13.13, 10.14.14.14, 10.15.15.15, "
       "10.16.16.16, 10.17.17.17, 10.18.18.18, 10.19.19.19, 10.20.20.20, 10.21.21.21"}};

  auto remote_address = std::make_shared<Network::Address::Ipv4Instance>("192.0.2.11");
  Envoy::Http::OriginalIPDetectionParams params = {headers, remote_address};
  auto result = xff_extension_->detect(params);
  ASSERT_EQ(result.detected_remote_address, nullptr);
}

TEST(XffInvalidConfigTest, InvalidConfig) {
  envoy::extensions::http::original_ip_detection::xff::v3::XffConfig config;
  config.set_xff_num_trusted_hops(1);
  auto cidr = config.mutable_xff_trusted_cidrs()->add_cidrs();
  cidr->set_address_prefix("192.0.2.0");
  cidr->mutable_prefix_len()->set_value(24);

  EXPECT_EQ(XffIPDetection::create(config).status().message(),
            "Cannot set both xff_num_trusted_hops and xff_trusted_cidrs");
}

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
