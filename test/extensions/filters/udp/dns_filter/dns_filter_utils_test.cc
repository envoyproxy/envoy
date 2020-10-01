#include "envoy/extensions/filters/udp/dns_filter/v3alpha/dns_filter.pb.h"
#include "envoy/extensions/filters/udp/dns_filter/v3alpha/dns_filter.pb.validate.h"

#include "common/network/address_impl.h"

#include "extensions/filters/udp/dns_filter/dns_filter_utils.h"

#include "test/test_common/environment.h"

#include "dns_filter_test_utils.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace Utils {
namespace {

class DnsFilterUtilsTest : public testing::Test {};

TEST_F(DnsFilterUtilsTest, UtilsProtoNameTest) {
  std::map<uint16_t, std::string> proto_name_test = {
      {6, "tcp"},
      {17, "udp"},
      {155, ""},
  };

  using envoy::data::dns::v3::DnsTable;

  for (const auto& proto : proto_name_test) {
    DnsTable::DnsServiceProtocol p;
    p.set_number(proto.first);
    const std::string proto_name = Utils::getProtoName(p);

    EXPECT_STREQ(proto_name.c_str(), proto.second.c_str());
  }
}

TEST_F(DnsFilterUtilsTest, ServiceNameSynthesisTest) {
  struct DnsServiceTestData {
    const std::string name;
    const std::string proto;
    const std::string domain;
    const std::string expected;
  } service_data[] = {
      // When creating the full service name, we prepend an underscore if necessary
      {"name1", "proto1", "test.com", "_name1._proto1.test.com"},
      {"name2", "_proto2", "test2.com", "_name2._proto2.test2.com"},
      {"_name3", "proto3", "test3.com", "_name3._proto3.test3.com"},
      {"name4", "proto4", "_sites.test4.com", "_name4._proto4._sites.test4.com"},
  };

  for (auto& ptr : service_data) {
    const std::string result = Utils::buildServiceName(ptr.name, ptr.proto, ptr.domain);
    EXPECT_STREQ(ptr.expected.c_str(), result.c_str());
  }
}

TEST_F(DnsFilterUtilsTest, ServiceNameParsingTest) {
  struct DnsServiceTestData {
    const std::string domain;
    const std::string expected_service;
    const std::string expected_proto;
  } service_data[] = {
      // Service names and protocols must begin with an underscore
      {"_ldap._tcp.Default-First-Site-Name._sites.dc._msdcs.utelsystems.local", "ldap", "tcp"},
      {"_ldap._tcp._sites.dc._msdcs.utelsystems.local", "ldap", "tcp"},
      {"_ldap._nottcp._sites.dc._msdcs.utelsystems.local", "ldap", "nottcp"},
      {"ldap.tcp._sites.dc._msdcs.utelsystems.local", "", ""},
      {".tcp._sites.dc._msdcs.utelsystems.local", "", ""},
      {"", "", ""},
  };

  for (auto& ptr : service_data) {
    const absl::string_view service = Utils::getServiceFromName(ptr.domain);
    const std::string service_str(service);
    EXPECT_STREQ(ptr.expected_service.c_str(), service_str.c_str());

    const absl::string_view proto = Utils::getProtoFromName(ptr.domain);
    const std::string proto_str(proto);
    EXPECT_STREQ(ptr.expected_proto.c_str(), proto_str.c_str());
  }
}

TEST_F(DnsFilterUtilsTest, GetAddressRecordTypeTest) {
  const std::string pipe_path(Platform::null_device_path);
  const auto pipe = std::make_shared<Network::Address::PipeInstance>(pipe_path, 600);
  auto addr_type = getAddressRecordType(pipe);
  EXPECT_EQ(addr_type, absl::nullopt);

  const auto ipv6addr = Network::Utility::parseInternetAddress("fec0:1::1", 0);
  addr_type = getAddressRecordType(ipv6addr);
  EXPECT_TRUE(addr_type.has_value());
  EXPECT_EQ(addr_type.value(), DNS_RECORD_TYPE_AAAA);

  const auto ipv4addr = Network::Utility::parseInternetAddress("127.0.0.1", 0);
  addr_type = getAddressRecordType(ipv4addr);
  EXPECT_TRUE(addr_type.has_value());
  EXPECT_EQ(addr_type.value(), DNS_RECORD_TYPE_A);
}

} // namespace
} // namespace Utils
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
