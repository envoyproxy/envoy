#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/type/matcher/v3/metadata.pb.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/common/expr/evaluator.h"
#include "source/extensions/filters/common/rbac/matchers.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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
    const Envoy::Http::RequestHeaderMap& headers = Envoy::Http::TestRequestHeaderMapImpl(),
    const StreamInfo::StreamInfo& info = NiceMock<StreamInfo::MockStreamInfo>()) {
  EXPECT_EQ(expected, matcher.matches(connection, headers, info));
}

PortRangeMatcher createPortRangeMatcher(envoy::type::v3::Int32Range range) { return {range}; }

TEST(AlwaysMatcher, AlwaysMatches) { checkMatcher(RBAC::AlwaysMatcher(), true); }

TEST(AndMatcher, Permission_Set) {
  envoy::config::rbac::v3::Permission::Set set;
  envoy::config::rbac::v3::Permission* perm = set.add_rules();
  perm->set_any(true);

  checkMatcher(RBAC::AndMatcher(set, ProtobufMessage::getStrictValidationVisitor()), true);

  perm = set.add_rules();
  perm->set_destination_port(123);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(RBAC::AndMatcher(set, ProtobufMessage::getStrictValidationVisitor()), true, conn,
               headers, info);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 8080, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(RBAC::AndMatcher(set, ProtobufMessage::getStrictValidationVisitor()), false, conn,
               headers, info);
}

TEST(AndMatcher, Principal_Set) {
  envoy::config::rbac::v3::Principal::Set set;
  envoy::config::rbac::v3::Principal* principal = set.add_ids();
  principal->set_any(true);

  checkMatcher(RBAC::AndMatcher(set), true);

  principal = set.add_ids();
  auto* cidr = principal->mutable_direct_remote_ip();
  cidr->set_address_prefix("1.2.3.0");
  cidr->mutable_prefix_len()->set_value(24);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);

  checkMatcher(RBAC::AndMatcher(set), true, conn, headers, info);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.4.6", 123, false);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);

  checkMatcher(RBAC::AndMatcher(set), false, conn, headers, info);
}

TEST(OrMatcher, Permission_Set) {
  envoy::config::rbac::v3::Permission::Set set;
  envoy::config::rbac::v3::Permission* perm = set.add_rules();
  perm->set_destination_port(123);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(RBAC::OrMatcher(set, ProtobufMessage::getStrictValidationVisitor()), false, conn,
               headers, info);

  perm = set.add_rules();
  perm->mutable_destination_port_range()->set_start(123);
  perm->mutable_destination_port_range()->set_end(456);

  checkMatcher(RBAC::OrMatcher(set, ProtobufMessage::getStrictValidationVisitor()), false, conn,
               headers, info);

  perm = set.add_rules();
  perm->set_any(true);

  checkMatcher(RBAC::OrMatcher(set, ProtobufMessage::getStrictValidationVisitor()), true, conn,
               headers, info);
}

TEST(OrMatcher, Principal_Set) {
  envoy::config::rbac::v3::Principal::Set set;
  envoy::config::rbac::v3::Principal* id = set.add_ids();
  auto* cidr = id->mutable_direct_remote_ip();
  cidr->set_address_prefix("1.2.3.0");
  cidr->mutable_prefix_len()->set_value(24);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.4.6", 456, false);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);

  checkMatcher(RBAC::OrMatcher(set), false, conn, headers, info);

  id = set.add_ids();
  id->set_any(true);

  checkMatcher(RBAC::OrMatcher(set), true, conn, headers, info);
}

TEST(NotMatcher, Permission) {
  envoy::config::rbac::v3::Permission perm;
  perm.set_any(true);

  checkMatcher(RBAC::NotMatcher(perm, ProtobufMessage::getStrictValidationVisitor()), false,
               Envoy::Network::MockConnection());
}

TEST(NotMatcher, Principal) {
  envoy::config::rbac::v3::Principal principal;
  principal.set_any(true);

  checkMatcher(RBAC::NotMatcher(principal), false, Envoy::Network::MockConnection());
}

TEST(HeaderMatcher, HeaderMatcher) {
  envoy::config::route::v3::HeaderMatcher config;
  config.set_name("foo");
  config.mutable_string_match()->set_exact("bar");

  Envoy::Http::TestRequestHeaderMapImpl headers;
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
  NiceMock<Envoy::Network::MockConnection> conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr connection_remote =
      Envoy::Network::Utility::parseInternetAddress("12.13.14.15", 789, false);
  Envoy::Network::Address::InstanceConstSharedPtr direct_local =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  Envoy::Network::Address::InstanceConstSharedPtr direct_remote =
      Envoy::Network::Utility::parseInternetAddress("4.5.6.7", 456, false);
  Envoy::Network::Address::InstanceConstSharedPtr downstream_remote =
      Envoy::Network::Utility::parseInternetAddress("8.9.10.11", 456, false);
  conn.stream_info_.downstream_connection_info_provider_->setRemoteAddress(connection_remote);
  info.downstream_connection_info_provider_->setLocalAddress(direct_local);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(direct_remote);
  info.downstream_connection_info_provider_->setRemoteAddress(downstream_remote);

  envoy::config::core::v3::CidrRange connection_remote_cidr;
  connection_remote_cidr.set_address_prefix("12.13.14.15");
  connection_remote_cidr.mutable_prefix_len()->set_value(32);

  envoy::config::core::v3::CidrRange downstream_local_cidr;
  downstream_local_cidr.set_address_prefix("1.2.3.0");
  downstream_local_cidr.mutable_prefix_len()->set_value(24);

  envoy::config::core::v3::CidrRange downstream_direct_remote_cidr;
  downstream_direct_remote_cidr.set_address_prefix("4.5.6.7");
  downstream_direct_remote_cidr.mutable_prefix_len()->set_value(32);

  envoy::config::core::v3::CidrRange downstream_remote_cidr;
  downstream_remote_cidr.set_address_prefix("8.9.10.11");
  downstream_remote_cidr.mutable_prefix_len()->set_value(32);

  checkMatcher(IPMatcher(connection_remote_cidr, IPMatcher::Type::ConnectionRemote), true, conn,
               headers, info);
  checkMatcher(IPMatcher(downstream_local_cidr, IPMatcher::Type::DownstreamLocal), true, conn,
               headers, info);
  checkMatcher(IPMatcher(downstream_direct_remote_cidr, IPMatcher::Type::DownstreamDirectRemote),
               true, conn, headers, info);
  checkMatcher(IPMatcher(downstream_remote_cidr, IPMatcher::Type::DownstreamRemote), true, conn,
               headers, info);

  connection_remote_cidr.set_address_prefix("4.5.6.7");
  downstream_local_cidr.set_address_prefix("1.2.4.8");
  downstream_direct_remote_cidr.set_address_prefix("4.5.6.0");
  downstream_remote_cidr.set_address_prefix("4.5.6.7");

  checkMatcher(IPMatcher(connection_remote_cidr, IPMatcher::Type::ConnectionRemote), false, conn,
               headers, info);
  checkMatcher(IPMatcher(downstream_local_cidr, IPMatcher::Type::DownstreamLocal), false, conn,
               headers, info);
  checkMatcher(IPMatcher(downstream_direct_remote_cidr, IPMatcher::Type::DownstreamDirectRemote),
               false, conn, headers, info);
  checkMatcher(IPMatcher(downstream_remote_cidr, IPMatcher::Type::DownstreamRemote), false, conn,
               headers, info);
}

TEST(PortMatcher, PortMatcher) {
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(PortMatcher(123), true, conn, headers, info);
  checkMatcher(PortMatcher(456), false, conn, headers, info);
}

// Test valid and invalid destination_port_range permission rule in RBAC.
TEST(PortRangeMatcher, PortRangeMatcher) {
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  // IP address with port 456 is in range [123, 789) and [456, 789), but not in range [123, 456) or
  // [12, 34).
  envoy::type::v3::Int32Range range;
  range.set_start(123);
  range.set_end(789);
  checkMatcher(PortRangeMatcher(range), true, conn, headers, info);

  range.set_start(456);
  range.set_end(789);
  checkMatcher(PortRangeMatcher(range), true, conn, headers, info);

  range.set_start(123);
  range.set_end(456);
  checkMatcher(PortRangeMatcher(range), false, conn, headers, info);

  range.set_start(12);
  range.set_end(34);
  checkMatcher(PortRangeMatcher(range), false, conn, headers, info);

  // Only IP address is valid for the permission rule.
  NiceMock<StreamInfo::MockStreamInfo> info2;
  Envoy::Network::Address::InstanceConstSharedPtr addr2 =
      std::make_shared<const Envoy::Network::Address::PipeInstance>("test");
  info2.downstream_connection_info_provider_->setLocalAddress(addr2);
  checkMatcher(PortRangeMatcher(range), false, conn, headers, info2);

  // Invalid rule will cause an exception.
  range.set_start(-1);
  range.set_end(80);
  EXPECT_THROW_WITH_REGEX(createPortRangeMatcher(range), EnvoyException,
                          "range start .* is out of bounds");

  range.set_start(80);
  range.set_end(65537);
  EXPECT_THROW_WITH_REGEX(createPortRangeMatcher(range), EnvoyException,
                          "range end .* is out of bounds");

  range.set_start(80);
  range.set_end(80);
  EXPECT_THROW_WITH_REGEX(createPortRangeMatcher(range), EnvoyException,
                          "range start .* cannot be greater or equal than range end .*");
}

TEST(AuthenticatedMatcher, uriSanPeerCertificate) {
  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();

  const std::vector<std::string> uri_sans{"foo", "baz"};
  const std::vector<std::string> dns_sans;
  const std::string subject = "subject";
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));

  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  // We should check if any URI SAN matches.
  envoy::config::rbac::v3::Principal::Authenticated auth;
  auth.mutable_principal_name()->set_exact("foo");
  checkMatcher(AuthenticatedMatcher(auth), true, conn);

  auth.mutable_principal_name()->set_exact("baz");
  checkMatcher(AuthenticatedMatcher(auth), true, conn);

  auth.mutable_principal_name()->set_exact("bar");
  checkMatcher(AuthenticatedMatcher(auth), false, conn);
}

TEST(AuthenticatedMatcher, dnsSanPeerCertificate) {
  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();

  const std::vector<std::string> uri_sans{"uri_foo"};
  const std::vector<std::string> dns_sans{"foo", "baz"};
  const std::string subject = "subject";

  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));

  // We should get check if any DNS SAN matches as URI SAN is not available.
  envoy::config::rbac::v3::Principal::Authenticated auth;
  auth.mutable_principal_name()->set_exact("foo");
  checkMatcher(AuthenticatedMatcher(auth), true, conn);

  auth.mutable_principal_name()->set_exact("baz");
  checkMatcher(AuthenticatedMatcher(auth), true, conn);

  auth.mutable_principal_name()->set_exact("bar");
  checkMatcher(AuthenticatedMatcher(auth), false, conn);
}

TEST(AuthenticatedMatcher, subjectPeerCertificate) {
  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();

  const std::vector<std::string> sans;
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(sans));
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(sans));
  std::string peer_subject = "bar";
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(peer_subject));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  envoy::config::rbac::v3::Principal::Authenticated auth;
  auth.mutable_principal_name()->set_exact("bar");
  checkMatcher(AuthenticatedMatcher(auth), true, conn);

  auth.mutable_principal_name()->set_exact("foo");
  checkMatcher(AuthenticatedMatcher(auth), false, conn);
}

TEST(AuthenticatedMatcher, AnySSLSubject) {
  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  const std::vector<std::string> sans{"foo", "baz"};
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(sans));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  envoy::config::rbac::v3::Principal::Authenticated auth;
  checkMatcher(AuthenticatedMatcher(auth), true, conn);

  auth.mutable_principal_name()->MergeFrom(TestUtility::createRegexMatcher(".*"));
  checkMatcher(AuthenticatedMatcher(auth), true, conn);
}

TEST(AuthenticatedMatcher, NoSSL) {
  Envoy::Network::MockConnection conn;
  EXPECT_CALL(Const(conn), ssl()).WillOnce(Return(nullptr));
  checkMatcher(AuthenticatedMatcher({}), false, conn);
}

TEST(MetadataMatcher, MetadataMatcher) {
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl header;
  NiceMock<StreamInfo::MockStreamInfo> info;

  auto label = MessageUtil::keyValueStruct("label", "prod");
  envoy::config::core::v3::Metadata metadata;
  metadata.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>("other", label));
  metadata.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>("rbac", label));
  EXPECT_CALL(Const(info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("rbac");
  matcher.add_path()->set_key("label");

  matcher.mutable_value()->mutable_string_match()->set_exact("test");
  checkMatcher(MetadataMatcher(matcher), false, conn, header, info);
  matcher.mutable_value()->mutable_string_match()->set_exact("prod");
  checkMatcher(MetadataMatcher(matcher), true, conn, header, info);
}

TEST(PolicyMatcher, PolicyMatcher) {
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_permissions()->set_destination_port(456);
  policy.add_principals()->mutable_authenticated()->mutable_principal_name()->set_exact("foo");
  policy.add_principals()->mutable_authenticated()->mutable_principal_name()->set_exact("bar");
  Expr::BuilderPtr builder = Expr::createBuilder(nullptr);

  RBAC::PolicyMatcher matcher(policy, builder.get(), ProtobufMessage::getStrictValidationVisitor());

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 456, false);

  const std::vector<std::string> uri_sans{"bar", "baz"};
  const std::vector<std::string> dns_sans;
  const std::string subject = "subject";
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).Times(4).WillRepeatedly(Return(uri_sans));
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));

  EXPECT_CALL(Const(conn), ssl()).Times(2).WillRepeatedly(Return(ssl));
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(matcher, true, conn, headers, info);

  EXPECT_CALL(Const(conn), ssl()).Times(2).WillRepeatedly(Return(nullptr));
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(matcher, false, conn, headers, info);

  addr = Envoy::Network::Utility::parseInternetAddress("1.2.3.4", 789, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(matcher, false, conn, headers, info);
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

TEST(PathMatcher, NoPathInHeader) {
  Envoy::Http::TestRequestHeaderMapImpl headers;
  envoy::type::matcher::v3::PathMatcher matcher;
  matcher.mutable_path()->mutable_safe_regex()->mutable_google_re2();
  matcher.mutable_path()->mutable_safe_regex()->set_regex(".*");

  headers.setPath("/path");
  checkMatcher(PathMatcher(matcher), true, Envoy::Network::MockConnection(), headers);
  headers.removePath();
  checkMatcher(PathMatcher(matcher), false, Envoy::Network::MockConnection(), headers);
}

TEST(PathMatcher, ValidPathInHeader) {
  Envoy::Http::TestRequestHeaderMapImpl headers;
  envoy::type::matcher::v3::PathMatcher matcher;
  matcher.mutable_path()->set_exact("/exact");

  headers.setPath("/exact");
  checkMatcher(PathMatcher(matcher), true, Envoy::Network::MockConnection(), headers);
  headers.setPath("/exact?param=val");
  checkMatcher(PathMatcher(matcher), true, Envoy::Network::MockConnection(), headers);
  headers.setPath("/exact#fragment");
  checkMatcher(PathMatcher(matcher), true, Envoy::Network::MockConnection(), headers);
  headers.setPath("/exacz");
  checkMatcher(PathMatcher(matcher), false, Envoy::Network::MockConnection(), headers);
}

class TestObject : public StreamInfo::FilterState::Object {
public:
  absl::optional<std::string> serializeAsString() const override { return "test.value"; }
};

TEST(FilterStateMatcher, FilterStateMatcher) {
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl header;
  NiceMock<StreamInfo::MockStreamInfo> info;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  EXPECT_CALL(Const(info), filterState()).WillRepeatedly(ReturnRef(filter_state));

  envoy::type::matcher::v3::FilterStateMatcher matcher;
  matcher.set_key("test.key");
  matcher.mutable_string_match()->set_prefix("test");

  checkMatcher(FilterStateMatcher(matcher), false, conn, header, info);
  filter_state.setData("test.key", std::make_shared<TestObject>(),
                       StreamInfo::FilterState::StateType::ReadOnly);
  checkMatcher(FilterStateMatcher(matcher), true, conn, header, info);
}

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
