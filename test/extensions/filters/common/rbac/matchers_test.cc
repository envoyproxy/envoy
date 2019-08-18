#include "common/network/utility.h"

#include "extensions/filters/common/rbac/matchers.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Const;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {
namespace {

void checkMatcher(
    const RBAC::Matcher& matcher, bool expected,
    const Envoy::Network::Connection& connection = Envoy::Network::MockConnection(),
    const Envoy::Http::HeaderMap& headers = Envoy::Http::HeaderMapImpl(),
    const envoy::api::v2::core::Metadata& metadata = envoy::api::v2::core::Metadata()) {
  EXPECT_EQ(expected, matcher.matches(connection, headers, metadata));
}

TEST(AlwaysMatcher, AlwaysMatches) { checkMatcher(RBAC::AlwaysMatcher(), true); }

TEST(AndMatcher, Permission_Set) {
  envoy::config::rbac::v2::Permission_Set set;
  envoy::config::rbac::v2::Permission* perm = set.add_rules();
  perm->set_any(true);

  checkMatcher(RBAC::AndMatcher(set), true);

  perm = set.add_rules();
  perm->set_destination_port(123);

  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(conn, localAddress()).WillOnce(ReturnRef(addr));

  checkMatcher(RBAC::AndMatcher(set), true, conn);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 8080, false);
  EXPECT_CALL(conn, localAddress()).WillOnce(ReturnRef(addr));

  checkMatcher(RBAC::AndMatcher(set), false, conn);
}

TEST(AndMatcher, Principal_Set) {
  envoy::config::rbac::v2::Principal_Set set;
  envoy::config::rbac::v2::Principal* principal = set.add_ids();
  principal->set_any(true);

  checkMatcher(RBAC::AndMatcher(set), true);

  principal = set.add_ids();
  auto* cidr = principal->mutable_source_ip();
  cidr->set_address_prefix("1.2.3.0");
  cidr->mutable_prefix_len()->set_value(24);

  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(conn, remoteAddress()).WillOnce(ReturnRef(addr));

  checkMatcher(RBAC::AndMatcher(set), true, conn);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.4.6", 123, false);
  EXPECT_CALL(conn, remoteAddress()).WillOnce(ReturnRef(addr));

  checkMatcher(RBAC::AndMatcher(set), false, conn);
}

TEST(OrMatcher, Permission_Set) {
  envoy::config::rbac::v2::Permission_Set set;
  envoy::config::rbac::v2::Permission* perm = set.add_rules();
  perm->set_destination_port(123);

  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  EXPECT_CALL(conn, localAddress()).Times(2).WillRepeatedly(ReturnRef(addr));

  checkMatcher(RBAC::OrMatcher(set), false, conn);

  perm = set.add_rules();
  perm->set_any(true);

  checkMatcher(RBAC::OrMatcher(set), true, conn);
}

TEST(OrMatcher, Principal_Set) {
  envoy::config::rbac::v2::Principal_Set set;
  envoy::config::rbac::v2::Principal* id = set.add_ids();
  auto* cidr = id->mutable_source_ip();
  cidr->set_address_prefix("1.2.3.0");
  cidr->mutable_prefix_len()->set_value(24);

  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.4.6", 456, false);
  EXPECT_CALL(conn, remoteAddress()).Times(2).WillRepeatedly(ReturnRef(addr));

  checkMatcher(RBAC::OrMatcher(set), false, conn);

  id = set.add_ids();
  id->set_any(true);

  checkMatcher(RBAC::OrMatcher(set), true, conn);
}

TEST(NotMatcher, Permission) {
  envoy::config::rbac::v2::Permission perm;
  perm.set_any(true);

  checkMatcher(RBAC::NotMatcher(perm), false, Envoy::Network::MockConnection());
}

TEST(NotMatcher, Principal) {
  envoy::config::rbac::v2::Principal principal;
  principal.set_any(true);

  checkMatcher(RBAC::NotMatcher(principal), false, Envoy::Network::MockConnection());
}

TEST(HeaderMatcher, HeaderMatcher) {
  envoy::api::v2::route::HeaderMatcher config;
  config.set_name("foo");
  config.set_exact_match("bar");

  Envoy::Http::HeaderMapImpl headers;
  Envoy::Http::LowerCaseString key("foo");
  std::string value = "bar";
  headers.setReference(key, value);

  RBAC::HeaderMatcher matcher(config);

  checkMatcher(matcher, true, Envoy::Network::MockConnection(), headers);

  value = "baz";
  headers.setReference(key, value);

  checkMatcher(matcher, false, Envoy::Network::MockConnection(), headers);
  checkMatcher(matcher, false);
}

TEST(IPMatcher, IPMatcher) {
  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr local =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  Envoy::Network::Address::InstanceConstSharedPtr remote =
      Envoy::Network::Utility::parseInternetAddress("4.5.6.7", 456, false);
  EXPECT_CALL(conn, localAddress()).Times(2).WillRepeatedly(ReturnRef(local));
  EXPECT_CALL(conn, remoteAddress()).Times(2).WillRepeatedly(ReturnRef(remote));

  envoy::api::v2::core::CidrRange local_cidr;
  local_cidr.set_address_prefix("1.2.3.0");
  local_cidr.mutable_prefix_len()->set_value(24);

  envoy::api::v2::core::CidrRange remote_cidr;
  remote_cidr.set_address_prefix("4.5.6.7");
  remote_cidr.mutable_prefix_len()->set_value(32);

  checkMatcher(IPMatcher(local_cidr, true), true, conn);
  checkMatcher(IPMatcher(remote_cidr, false), true, conn);

  local_cidr.set_address_prefix("1.2.4.8");
  remote_cidr.set_address_prefix("4.5.6.0");

  checkMatcher(IPMatcher(local_cidr, true), false, conn);
  checkMatcher(IPMatcher(remote_cidr, false), false, conn);
}

TEST(PortMatcher, PortMatcher) {
  Envoy::Network::MockConnection conn;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  EXPECT_CALL(conn, localAddress()).Times(2).WillRepeatedly(ReturnRef(addr));

  checkMatcher(PortMatcher(123), true, conn);
  checkMatcher(PortMatcher(456), false, conn);
}

TEST(AuthenticatedMatcher, uriSanPeerCertificate) {
  Envoy::Network::MockConnection conn;
  Envoy::Ssl::MockConnectionInfo ssl;

  const std::vector<std::string> sans{"foo", "baz"};
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillRepeatedly(Return(sans));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(&ssl));

  // We should get the first URI SAN.
  envoy::config::rbac::v2::Principal_Authenticated auth;
  auth.mutable_principal_name()->set_exact("foo");
  checkMatcher(AuthenticatedMatcher(auth), true, conn);

  auth.mutable_principal_name()->set_exact("bar");
  checkMatcher(AuthenticatedMatcher(auth), false, conn);
}

TEST(AuthenticatedMatcher, dnsSanPeerCertificate) {
  Envoy::Network::MockConnection conn;
  Envoy::Ssl::MockConnectionInfo ssl;

  const std::vector<std::string> uri_sans;
  const std::vector<std::string> dns_sans{"foo", "baz"};

  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(&ssl));

  EXPECT_CALL(ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(&ssl));

  // We should get the first DNS SAN as URI SAN is not available.
  envoy::config::rbac::v2::Principal_Authenticated auth;
  auth.mutable_principal_name()->set_exact("foo");
  checkMatcher(AuthenticatedMatcher(auth), true, conn);

  auth.mutable_principal_name()->set_exact("bar");
  checkMatcher(AuthenticatedMatcher(auth), false, conn);
}

TEST(AuthenticatedMatcher, subjectPeerCertificate) {
  Envoy::Network::MockConnection conn;
  Envoy::Ssl::MockConnectionInfo ssl;

  const std::vector<std::string> sans;
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillRepeatedly(Return(sans));
  EXPECT_CALL(ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(sans));
  EXPECT_CALL(ssl, subjectPeerCertificate()).WillRepeatedly(Return("bar"));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(&ssl));

  envoy::config::rbac::v2::Principal_Authenticated auth;
  auth.mutable_principal_name()->set_exact("bar");
  checkMatcher(AuthenticatedMatcher(auth), true, conn);

  auth.mutable_principal_name()->set_exact("foo");
  checkMatcher(AuthenticatedMatcher(auth), false, conn);
}

TEST(AuthenticatedMatcher, AnySSLSubject) {
  Envoy::Network::MockConnection conn;
  Envoy::Ssl::MockConnectionInfo ssl;
  const std::vector<std::string> sans{"foo", "baz"};
  EXPECT_CALL(ssl, uriSanPeerCertificate()).WillRepeatedly(Return(sans));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(&ssl));

  envoy::config::rbac::v2::Principal_Authenticated auth;
  checkMatcher(AuthenticatedMatcher(auth), true, conn);

  auth.mutable_principal_name()->set_regex(".*");
  checkMatcher(AuthenticatedMatcher(auth), true, conn);
}

TEST(AuthenticatedMatcher, NoSSL) {
  Envoy::Network::MockConnection conn;
  EXPECT_CALL(Const(conn), ssl()).WillOnce(Return(nullptr));
  checkMatcher(AuthenticatedMatcher({}), false, conn);
}

TEST(MetadataMatcher, MetadataMatcher) {
  Envoy::Network::MockConnection conn;
  Envoy::Http::HeaderMapImpl header;

  auto label = MessageUtil::keyValueStruct("label", "prod");
  envoy::api::v2::core::Metadata metadata;
  metadata.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>("other", label));
  metadata.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>("rbac", label));

  envoy::type::matcher::MetadataMatcher matcher;
  matcher.set_filter("rbac");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  checkMatcher(MetadataMatcher(matcher), false, conn, header, metadata);
  matcher.mutable_value()->mutable_string_match()->set_exact("prod");
  checkMatcher(MetadataMatcher(matcher), true, conn, header, metadata);
}

TEST(PolicyMatcher, PolicyMatcher) {
  envoy::config::rbac::v2::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_permissions()->set_destination_port(456);
  policy.add_principals()->mutable_authenticated()->mutable_principal_name()->set_exact("foo");
  policy.add_principals()->mutable_authenticated()->mutable_principal_name()->set_exact("bar");

  RBAC::PolicyMatcher matcher(policy);

  Envoy::Network::MockConnection conn;
  Envoy::Ssl::MockConnectionInfo ssl;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);

  const std::vector<std::string> sans{"bar", "baz"};
  EXPECT_CALL(ssl, uriSanPeerCertificate()).Times(2).WillRepeatedly(Return(sans));
  EXPECT_CALL(Const(conn), ssl()).Times(2).WillRepeatedly(Return(&ssl));
  EXPECT_CALL(conn, localAddress()).Times(2).WillRepeatedly(ReturnRef(addr));

  checkMatcher(matcher, true, conn);

  EXPECT_CALL(Const(conn), ssl()).Times(2).WillRepeatedly(Return(nullptr));
  EXPECT_CALL(conn, localAddress()).Times(2).WillRepeatedly(ReturnRef(addr));

  checkMatcher(matcher, false, conn);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 789, false);
  EXPECT_CALL(conn, localAddress()).Times(2).WillRepeatedly(ReturnRef(addr));

  checkMatcher(matcher, false, conn);
}

TEST(RequestedServerNameMatcher, ValidRequestedServerName) {
  Envoy::Network::MockConnection conn;
  EXPECT_CALL(conn, requestedServerName())
      .Times(9)
      .WillRepeatedly(Return(absl::string_view("www.cncf.io")));

  checkMatcher(RequestedServerNameMatcher(TestUtility::createRegexMatcher(".*cncf.io")), true,
               conn);
  checkMatcher(RequestedServerNameMatcher(TestUtility::createRegexMatcher(".*cncf.*")), true, conn);
  checkMatcher(RequestedServerNameMatcher(TestUtility::createRegexMatcher("www.*")), true, conn);
  checkMatcher(RequestedServerNameMatcher(TestUtility::createRegexMatcher(".*io")), true, conn);
  checkMatcher(RequestedServerNameMatcher(TestUtility::createRegexMatcher(".*")), true, conn);

  checkMatcher(RequestedServerNameMatcher(TestUtility::createExactMatcher("")), false, conn);
  checkMatcher(RequestedServerNameMatcher(TestUtility::createExactMatcher("www.cncf.io")), true,
               conn);
  checkMatcher(RequestedServerNameMatcher(TestUtility::createExactMatcher("xyz.cncf.io")), false,
               conn);
  checkMatcher(RequestedServerNameMatcher(TestUtility::createExactMatcher("example.com")), false,
               conn);
}

TEST(RequestedServerNameMatcher, EmptyRequestedServerName) {
  Envoy::Network::MockConnection conn;
  EXPECT_CALL(conn, requestedServerName()).Times(3).WillRepeatedly(Return(absl::string_view("")));

  checkMatcher(RequestedServerNameMatcher(TestUtility::createRegexMatcher(".*")), true, conn);

  checkMatcher(RequestedServerNameMatcher(TestUtility::createExactMatcher("")), true, conn);
  checkMatcher(RequestedServerNameMatcher(TestUtility::createExactMatcher("example.com")), false,
               conn);
}

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
