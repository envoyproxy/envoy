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
#include "source/extensions/filters/common/rbac/principals/mtls_authenticated/mtls_authenticated.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/status_utility.h"

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
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Permission::Set set;
  envoy::config::rbac::v3::Permission* perm = set.add_rules();
  perm->set_any(true);

  checkMatcher(
      RBAC::AndMatcher(set, ProtobufMessage::getStrictValidationVisitor(), factory_context), true);

  perm = set.add_rules();
  perm->set_destination_port(123);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(
      RBAC::AndMatcher(set, ProtobufMessage::getStrictValidationVisitor(), factory_context), true,
      conn, headers, info);

  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 8080, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(
      RBAC::AndMatcher(set, ProtobufMessage::getStrictValidationVisitor(), factory_context), false,
      conn, headers, info);
}

TEST(AndMatcher, Principal_Set) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Principal::Set set;
  envoy::config::rbac::v3::Principal* principal = set.add_ids();
  principal->set_any(true);

  checkMatcher(RBAC::AndMatcher(set, factory_context), true);

  principal = set.add_ids();
  auto* cidr = principal->mutable_direct_remote_ip();
  cidr->set_address_prefix("1.2.3.0");
  cidr->mutable_prefix_len()->set_value(24);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);

  checkMatcher(RBAC::AndMatcher(set, factory_context), true, conn, headers, info);

  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.4.6", 123, false);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);

  checkMatcher(RBAC::AndMatcher(set, factory_context), false, conn, headers, info);
}

TEST(OrMatcher, Permission_Set) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Permission::Set set;
  envoy::config::rbac::v3::Permission* perm = set.add_rules();
  perm->set_destination_port(123);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 456, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(RBAC::OrMatcher(set, ProtobufMessage::getStrictValidationVisitor(), factory_context),
               false, conn, headers, info);

  perm = set.add_rules();
  perm->mutable_destination_port_range()->set_start(123);
  perm->mutable_destination_port_range()->set_end(456);

  checkMatcher(RBAC::OrMatcher(set, ProtobufMessage::getStrictValidationVisitor(), factory_context),
               false, conn, headers, info);

  perm = set.add_rules();
  perm->set_any(true);

  checkMatcher(RBAC::OrMatcher(set, ProtobufMessage::getStrictValidationVisitor(), factory_context),
               true, conn, headers, info);
}

TEST(OrMatcher, Principal_Set) {
  envoy::config::rbac::v3::Principal::Set set;
  envoy::config::rbac::v3::Principal* id = set.add_ids();
  auto* cidr = id->mutable_direct_remote_ip();
  cidr->set_address_prefix("1.2.3.0");
  cidr->mutable_prefix_len()->set_value(24);

  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.4.6", 456, false);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);

  checkMatcher(RBAC::OrMatcher(set, factory_context), false, conn, headers, info);

  id = set.add_ids();
  id->set_any(true);

  checkMatcher(RBAC::OrMatcher(set, factory_context), true, conn, headers, info);
}

TEST(NotMatcher, Permission) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Permission perm;
  perm.set_any(true);

  checkMatcher(
      RBAC::NotMatcher(perm, ProtobufMessage::getStrictValidationVisitor(), factory_context), false,
      Envoy::Network::MockConnection());
}

TEST(NotMatcher, Principal) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Principal principal;
  principal.set_any(true);

  checkMatcher(RBAC::NotMatcher(principal, factory_context), false,
               Envoy::Network::MockConnection());
}

TEST(HeaderMatcher, HeaderMatcher) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::route::v3::HeaderMatcher config;
  config.set_name("foo");
  config.mutable_string_match()->set_exact("bar");

  Envoy::Http::TestRequestHeaderMapImpl headers;
  Envoy::Http::LowerCaseString key("foo");
  std::string value = "bar";
  headers.setReference(key, value);

  RBAC::HeaderMatcher matcher(config, factory_context);

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
      Envoy::Network::Utility::parseInternetAddressNoThrow("12.13.14.15", 789, false);
  Envoy::Network::Address::InstanceConstSharedPtr direct_local =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  Envoy::Network::Address::InstanceConstSharedPtr direct_remote =
      Envoy::Network::Utility::parseInternetAddressNoThrow("4.5.6.7", 456, false);
  Envoy::Network::Address::InstanceConstSharedPtr downstream_remote =
      Envoy::Network::Utility::parseInternetAddressNoThrow("8.9.10.11", 456, false);
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

  auto connection_remote_matcher =
      IPMatcher::create(connection_remote_cidr, IPMatcher::Type::ConnectionRemote);
  ASSERT_OK(connection_remote_matcher);
  checkMatcher(*connection_remote_matcher.value(), true, conn, headers, info);

  auto downstream_local_matcher =
      IPMatcher::create(downstream_local_cidr, IPMatcher::Type::DownstreamLocal);
  ASSERT_OK(downstream_local_matcher);
  checkMatcher(*downstream_local_matcher.value(), true, conn, headers, info);

  auto downstream_direct_remote_matcher =
      IPMatcher::create(downstream_direct_remote_cidr, IPMatcher::Type::DownstreamDirectRemote);
  ASSERT_OK(downstream_direct_remote_matcher);
  checkMatcher(*downstream_direct_remote_matcher.value(), true, conn, headers, info);

  auto downstream_remote_matcher =
      IPMatcher::create(downstream_remote_cidr, IPMatcher::Type::DownstreamRemote);
  ASSERT_OK(downstream_remote_matcher);
  checkMatcher(*downstream_remote_matcher.value(), true, conn, headers, info);

  connection_remote_cidr.set_address_prefix("4.5.6.7");
  downstream_local_cidr.set_address_prefix("1.2.4.8");
  downstream_direct_remote_cidr.set_address_prefix("4.5.6.0");
  downstream_remote_cidr.set_address_prefix("4.5.6.7");

  auto connection_remote_matcher2 =
      IPMatcher::create(connection_remote_cidr, IPMatcher::Type::ConnectionRemote);
  ASSERT_OK(connection_remote_matcher2);
  checkMatcher(*connection_remote_matcher2.value(), false, conn, headers, info);

  auto downstream_local_matcher2 =
      IPMatcher::create(downstream_local_cidr, IPMatcher::Type::DownstreamLocal);
  ASSERT_OK(downstream_local_matcher2);
  checkMatcher(*downstream_local_matcher2.value(), false, conn, headers, info);

  auto downstream_direct_remote_matcher2 =
      IPMatcher::create(downstream_direct_remote_cidr, IPMatcher::Type::DownstreamDirectRemote);
  ASSERT_OK(downstream_direct_remote_matcher2);
  checkMatcher(*downstream_direct_remote_matcher2.value(), false, conn, headers, info);

  auto downstream_remote_matcher2 =
      IPMatcher::create(downstream_remote_cidr, IPMatcher::Type::DownstreamRemote);
  ASSERT_OK(downstream_remote_matcher2);
  checkMatcher(*downstream_remote_matcher2.value(), false, conn, headers, info);
}

// Ensure non-IP addresses (e.g., pipe) do not crash IPMatcher and simply return false.
TEST(IPMatcher, NonIpAddressesReturnFalseAndDoNotCrash) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  // Principal: source_ip (ConnectionRemote)
  {
    envoy::config::rbac::v3::Principal principal;
    auto* cidr = principal.mutable_source_ip();
    cidr->set_address_prefix("::");
    cidr->mutable_prefix_len()->set_value(0);

    auto matcher = Matcher::create(principal, factory_context);
    ASSERT_NE(matcher, nullptr);

    NiceMock<Envoy::Network::MockConnection> conn;
    Envoy::Http::TestRequestHeaderMapImpl headers;
    NiceMock<StreamInfo::MockStreamInfo> info;

    Envoy::Network::Address::InstanceConstSharedPtr pipe =
        *Envoy::Network::Address::PipeInstance::create("test");
    conn.stream_info_.downstream_connection_info_provider_->setRemoteAddress(pipe);
    EXPECT_FALSE(matcher->matches(conn, headers, info));
  }

  // Permission: destination_ip (DownstreamLocal)
  {
    envoy::config::rbac::v3::Permission permission;
    auto* cidr = permission.mutable_destination_ip();
    cidr->set_address_prefix("::");
    cidr->mutable_prefix_len()->set_value(0);

    auto matcher =
        Matcher::create(permission, ProtobufMessage::getStrictValidationVisitor(), factory_context);
    ASSERT_NE(matcher, nullptr);

    NiceMock<Envoy::Network::MockConnection> conn;
    Envoy::Http::TestRequestHeaderMapImpl headers;
    NiceMock<StreamInfo::MockStreamInfo> info;

    Envoy::Network::Address::InstanceConstSharedPtr pipe =
        *Envoy::Network::Address::PipeInstance::create("test");
    info.downstream_connection_info_provider_->setLocalAddress(pipe);
    EXPECT_FALSE(matcher->matches(conn, headers, info));
  }

  // Principal: direct_remote_ip (DownstreamDirectRemote)
  {
    envoy::config::rbac::v3::Principal principal;
    auto* cidr = principal.mutable_direct_remote_ip();
    cidr->set_address_prefix("::");
    cidr->mutable_prefix_len()->set_value(0);

    auto matcher = Matcher::create(principal, factory_context);
    ASSERT_NE(matcher, nullptr);

    NiceMock<Envoy::Network::MockConnection> conn;
    Envoy::Http::TestRequestHeaderMapImpl headers;
    NiceMock<StreamInfo::MockStreamInfo> info;

    Envoy::Network::Address::InstanceConstSharedPtr pipe =
        *Envoy::Network::Address::PipeInstance::create("test");
    info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(pipe);
    EXPECT_FALSE(matcher->matches(conn, headers, info));
  }

  // Principal: remote_ip (DownstreamRemote)
  {
    envoy::config::rbac::v3::Principal principal;
    auto* cidr = principal.mutable_remote_ip();
    cidr->set_address_prefix("::");
    cidr->mutable_prefix_len()->set_value(0);

    auto matcher = Matcher::create(principal, factory_context);
    ASSERT_NE(matcher, nullptr);

    NiceMock<Envoy::Network::MockConnection> conn;
    Envoy::Http::TestRequestHeaderMapImpl headers;
    NiceMock<StreamInfo::MockStreamInfo> info;

    Envoy::Network::Address::InstanceConstSharedPtr pipe =
        *Envoy::Network::Address::PipeInstance::create("test");
    info.downstream_connection_info_provider_->setRemoteAddress(pipe);
    EXPECT_FALSE(matcher->matches(conn, headers, info));
  }
}

TEST(PortMatcher, PortMatcher) {
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
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
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 456, false);
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
      *Envoy::Network::Address::PipeInstance::create("test");
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
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Principal::Authenticated auth;
  auth.mutable_principal_name()->set_exact("foo");
  checkMatcher(AuthenticatedMatcher(auth, factory_context), true, conn);

  auth.mutable_principal_name()->set_exact("baz");
  checkMatcher(AuthenticatedMatcher(auth, factory_context), true, conn);

  auth.mutable_principal_name()->set_exact("bar");
  checkMatcher(AuthenticatedMatcher(auth, factory_context), false, conn);
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
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  auth.mutable_principal_name()->set_exact("foo");
  checkMatcher(AuthenticatedMatcher(auth, factory_context), true, conn);

  auth.mutable_principal_name()->set_exact("baz");
  checkMatcher(AuthenticatedMatcher(auth, factory_context), true, conn);

  auth.mutable_principal_name()->set_exact("bar");
  checkMatcher(AuthenticatedMatcher(auth, factory_context), false, conn);
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
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  auth.mutable_principal_name()->set_exact("bar");
  checkMatcher(AuthenticatedMatcher(auth, factory_context), true, conn);

  auth.mutable_principal_name()->set_exact("foo");
  checkMatcher(AuthenticatedMatcher(auth, factory_context), false, conn);
}

TEST(AuthenticatedMatcher, AnySSLSubject) {
  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  const std::vector<std::string> sans{"foo", "baz"};
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(sans));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  envoy::config::rbac::v3::Principal::Authenticated auth;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  checkMatcher(AuthenticatedMatcher(auth, factory_context), true, conn);

  auth.mutable_principal_name()->MergeFrom(TestUtility::createRegexMatcher(".*"));
  checkMatcher(AuthenticatedMatcher(auth, factory_context), true, conn);
}

TEST(AuthenticatedMatcher, NoSSL) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  Envoy::Network::MockConnection conn;
  EXPECT_CALL(Const(conn), ssl()).WillOnce(Return(nullptr));
  checkMatcher(AuthenticatedMatcher({}, factory_context), false, conn);
}

TEST(MtlsAuthenticatedMatcher, ValidateConfig) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  envoy::extensions::rbac::principals::mtls_authenticated::v3::Config
      mtls_authenticated; // Leave empty which is invalid.
  envoy::config::rbac::v3::Principal principal;
  principal.mutable_custom()->mutable_typed_config()->PackFrom(mtls_authenticated);
  EXPECT_THROW_WITH_MESSAGE(
      { Matcher::create(principal, factory_context); }, EnvoyException,
      "envoy.rbac.principals.mtls_authenticated did not have any configured "
      "validation. At least one configuration field must be set.");
}

// This matcher will not match in any configuration if the connection is not ssl.
TEST(MtlsAuthenticatedMatcher, NoSSL) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  ON_CALL(factory_context, messageValidationVisitor())
      .WillByDefault(ReturnRef(ProtobufMessage::getNullValidationVisitor()));
  Envoy::Network::MockConnection conn;
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(nullptr));

  envoy::extensions::rbac::principals::mtls_authenticated::v3::Config mtls_authenticated;
  mtls_authenticated.set_any_validated_client_certificate(true);
  envoy::config::rbac::v3::Principal principal;
  principal.mutable_custom()->mutable_typed_config()->PackFrom(mtls_authenticated);
  checkMatcher(*Matcher::create(principal, factory_context), false, conn);

  auto* matcher = mtls_authenticated.mutable_san_matcher();
  matcher->set_san_type(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI);
  matcher->mutable_matcher()->MergeFrom(TestUtility::createRegexMatcher(".*"));
  principal.mutable_custom()->mutable_typed_config()->PackFrom(mtls_authenticated);
  checkMatcher(*Matcher::create(principal, factory_context), false, conn);
}

// This matcher will not match in any configuration if the peer certificate is not validated.
TEST(MtlsAuthenticatedMatcher, UnvalidatedPeerCertificate) {
  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  const std::vector<std::string> sans{"foo", "baz"};
  EXPECT_CALL(*ssl, peerCertificateValidated()).WillRepeatedly(Return(false));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  envoy::extensions::rbac::principals::mtls_authenticated::v3::Config auth;
  auth.set_any_validated_client_certificate(true);
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  checkMatcher(Principals::MtlsAuthenticatedMatcher(auth, factory_context), false, conn);

  auto* matcher = auth.mutable_san_matcher();
  matcher->set_san_type(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI);
  matcher->mutable_matcher()->MergeFrom(TestUtility::createRegexMatcher(".*"));
  checkMatcher(Principals::MtlsAuthenticatedMatcher(auth, factory_context), false, conn);
}

TEST(MtlsAuthenticatedMatcher, AnyValidatedClientCertificate) {
  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  const std::vector<std::string> sans{"foo", "baz"};
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  envoy::extensions::rbac::principals::mtls_authenticated::v3::Config auth;
  auth.set_any_validated_client_certificate(true);
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  EXPECT_CALL(*ssl, peerCertificateValidated()).WillRepeatedly(Return(true));
  checkMatcher(Principals::MtlsAuthenticatedMatcher(auth, factory_context), true, conn);

  EXPECT_CALL(*ssl, peerCertificateValidated()).WillRepeatedly(Return(false));
  checkMatcher(Principals::MtlsAuthenticatedMatcher(auth, factory_context), false, conn);
}

TEST(MtlsAuthenticatedMatcher, SanMatcher) {
  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  const std::vector<std::string> sans{"foo", "baz"};
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));
  EXPECT_CALL(*ssl, peerCertificateValidated()).WillRepeatedly(Return(true));
  GENERAL_NAME san{};
  CSmartPtr<ASN1_IA5STRING, ASN1_IA5STRING_free> free_str = ASN1_IA5STRING_new();
  san.d.dNSName = free_str.get();

  // Use this invoke to validate that the passed in matcher is correct, instead of just returning
  // true or false directly in an expectation on this mock function.
  EXPECT_CALL(*ssl, peerCertificateSanMatches(_))
      .WillRepeatedly(
          Invoke([&](const Ssl::SanMatcher& matcher) -> bool { return matcher.match(&san); }));

  envoy::extensions::rbac::principals::mtls_authenticated::v3::Config auth;
  auto* matcher = auth.mutable_san_matcher();
  matcher->set_san_type(envoy::extensions::transport_sockets::tls::v3::SubjectAltNameMatcher::URI);
  matcher->mutable_matcher()->MergeFrom(TestUtility::createExactMatcher("my_san"));
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  // Correct name but wrong type.
  san.type = GEN_DNS;
  absl::string_view name("my_san");
  ASN1_STRING_set(san.d.dNSName, name.data(), name.length());
  checkMatcher(Principals::MtlsAuthenticatedMatcher(auth, factory_context), false, conn);

  // Correct name and type.
  san.type = GEN_URI;
  checkMatcher(Principals::MtlsAuthenticatedMatcher(auth, factory_context), true, conn);

  // Correct type but wrong name.
  name = "wrong";
  ASN1_STRING_set(san.d.dNSName, name.data(), name.length());
  checkMatcher(Principals::MtlsAuthenticatedMatcher(auth, factory_context), false, conn);
}

TEST(MetadataMatcher, MetadataMatcher) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
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
  checkMatcher(MetadataMatcher(Matchers::MetadataMatcher(matcher, context),
                               envoy::config::rbac::v3::MetadataSource::DYNAMIC),
               false, conn, header, info);
  matcher.mutable_value()->mutable_string_match()->set_exact("prod");
  checkMatcher(MetadataMatcher(Matchers::MetadataMatcher(matcher, context),
                               envoy::config::rbac::v3::MetadataSource::DYNAMIC),
               true, conn, header, info);
}

TEST(PolicyMatcher, PolicyMatcher) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Policy policy;
  policy.add_permissions()->set_destination_port(123);
  policy.add_permissions()->set_destination_port(456);
  policy.add_principals()->mutable_authenticated()->mutable_principal_name()->set_exact("foo");
  policy.add_principals()->mutable_authenticated()->mutable_principal_name()->set_exact("bar");
  Expr::BuilderPtr builder = Expr::createBuilder(nullptr);

  RBAC::PolicyMatcher matcher(policy, builder.get(), ProtobufMessage::getStrictValidationVisitor(),
                              factory_context);

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 456, false);

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

  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 789, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr);

  checkMatcher(matcher, false, conn, headers, info);
}

TEST(RequestedServerNameMatcher, ValidRequestedServerName) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  Envoy::Network::MockConnection conn;
  EXPECT_CALL(conn, requestedServerName())
      .Times(9)
      .WillRepeatedly(Return(absl::string_view("www.cncf.io")));

  checkMatcher(
      RequestedServerNameMatcher(TestUtility::createRegexMatcher(".*cncf.io"), factory_context),
      true, conn);
  checkMatcher(
      RequestedServerNameMatcher(TestUtility::createRegexMatcher(".*cncf.*"), factory_context),
      true, conn);
  checkMatcher(
      RequestedServerNameMatcher(TestUtility::createRegexMatcher("www.*"), factory_context), true,
      conn);
  checkMatcher(RequestedServerNameMatcher(TestUtility::createRegexMatcher(".*io"), factory_context),
               true, conn);
  checkMatcher(RequestedServerNameMatcher(TestUtility::createRegexMatcher(".*"), factory_context),
               true, conn);

  checkMatcher(RequestedServerNameMatcher(TestUtility::createExactMatcher(""), factory_context),
               false, conn);
  checkMatcher(
      RequestedServerNameMatcher(TestUtility::createExactMatcher("www.cncf.io"), factory_context),
      true, conn);
  checkMatcher(
      RequestedServerNameMatcher(TestUtility::createExactMatcher("xyz.cncf.io"), factory_context),
      false, conn);
  checkMatcher(
      RequestedServerNameMatcher(TestUtility::createExactMatcher("example.com"), factory_context),
      false, conn);
}

TEST(RequestedServerNameMatcher, EmptyRequestedServerName) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  Envoy::Network::MockConnection conn;
  EXPECT_CALL(conn, requestedServerName()).Times(3).WillRepeatedly(Return(absl::string_view("")));

  checkMatcher(RequestedServerNameMatcher(TestUtility::createRegexMatcher(".*"), factory_context),
               true, conn);

  checkMatcher(RequestedServerNameMatcher(TestUtility::createExactMatcher(""), factory_context),
               true, conn);
  checkMatcher(
      RequestedServerNameMatcher(TestUtility::createExactMatcher("example.com"), factory_context),
      false, conn);
}

TEST(PathMatcher, NoPathInHeader) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  envoy::type::matcher::v3::PathMatcher matcher;
  matcher.mutable_path()->mutable_safe_regex()->mutable_google_re2();
  matcher.mutable_path()->mutable_safe_regex()->set_regex(".*");

  headers.setPath("/path");
  checkMatcher(PathMatcher(matcher, context), true, Envoy::Network::MockConnection(), headers);
  headers.removePath();
  checkMatcher(PathMatcher(matcher, context), false, Envoy::Network::MockConnection(), headers);
}

TEST(PathMatcher, ValidPathInHeader) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  envoy::type::matcher::v3::PathMatcher matcher;
  matcher.mutable_path()->set_exact("/exact");

  headers.setPath("/exact");
  checkMatcher(PathMatcher(matcher, context), true, Envoy::Network::MockConnection(), headers);
  headers.setPath("/exact?param=val");
  checkMatcher(PathMatcher(matcher, context), true, Envoy::Network::MockConnection(), headers);
  headers.setPath("/exact#fragment");
  checkMatcher(PathMatcher(matcher, context), true, Envoy::Network::MockConnection(), headers);
  headers.setPath("/exacz");
  checkMatcher(PathMatcher(matcher, context), false, Envoy::Network::MockConnection(), headers);
}

class TestObject : public StreamInfo::FilterState::Object {
public:
  absl::optional<std::string> serializeAsString() const override { return "test.value"; }
};

TEST(FilterStateMatcher, FilterStateMatcher) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl header;
  NiceMock<StreamInfo::MockStreamInfo> info;
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  EXPECT_CALL(Const(info), filterState()).WillRepeatedly(ReturnRef(filter_state));

  envoy::type::matcher::v3::FilterStateMatcher matcher;
  matcher.set_key("test.key");
  matcher.mutable_string_match()->set_prefix("test");

  checkMatcher(FilterStateMatcher(matcher, context), false, conn, header, info);
  filter_state.setData("test.key", std::make_shared<TestObject>(),
                       StreamInfo::FilterState::StateType::ReadOnly);
  checkMatcher(FilterStateMatcher(matcher, context), true, conn, header, info);
}

TEST(UriTemplateMatcher, UriTemplateMatcherFactory) {
  const std::string yaml_string = R"EOF(
      name: envoy.path.match.uri_template.uri_template_matcher
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.path.match.uri_template.v3.UriTemplateMatchConfig
        path_template: "/bar/{lang}/{country}"
)EOF";
  Envoy::Http::TestRequestHeaderMapImpl headers;
  headers.setPath("/bar/lang/country");

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  const auto& factory =
      &Envoy::Config::Utility::getAndCheckFactory<Router::PathMatcherFactory>(config);

  EXPECT_NE(nullptr, factory);
  EXPECT_EQ(factory->name(), "envoy.path.match.uri_template.uri_template_matcher");
  EXPECT_EQ(factory->category(), "envoy.path.match");

  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), *factory);

  const auto uri_template_matcher = UriTemplateMatcher(factory->createPathMatcher(*message));

  checkMatcher(uri_template_matcher, true, Envoy::Network::MockConnection(), headers);
}

TEST(UriTemplateMatcher, NoPathInHeader) {
  Envoy::Http::TestRequestHeaderMapImpl headers;
  envoy::extensions::path::match::uri_template::v3::UriTemplateMatchConfig
      uri_template_match_config;
  const std::string path_template = "/{foo}";
  uri_template_match_config.set_path_template(path_template);
  Router::PathMatcherSharedPtr matcher =
      std::make_shared<Envoy::Extensions::UriTemplate::Match::UriTemplateMatcher>(
          uri_template_match_config);

  headers.setPath("/foo");
  checkMatcher(UriTemplateMatcher(matcher), true, Envoy::Network::MockConnection(), headers);
  headers.removePath();
  checkMatcher(UriTemplateMatcher(matcher), false, Envoy::Network::MockConnection(), headers);
}

TEST(MetadataMatcher, SourcedMetadataMatcher) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl header;
  NiceMock<StreamInfo::MockStreamInfo> info;
  std::shared_ptr<Router::MockRoute> route = std::make_shared<Router::MockRoute>();

  // Set up dynamic metadata
  auto dynamic_label = MessageUtil::keyValueStruct("dynamic_key", "dynamic_value");
  envoy::config::core::v3::Metadata dynamic_metadata;
  dynamic_metadata.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>("rbac", dynamic_label));
  EXPECT_CALL(Const(info), dynamicMetadata()).WillRepeatedly(ReturnRef(dynamic_metadata));

  // Set up route metadata
  auto route_label = MessageUtil::keyValueStruct("route_key", "route_value");
  envoy::config::core::v3::Metadata route_metadata;
  route_metadata.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>("rbac", route_label));
  EXPECT_CALL(*route, metadata()).WillRepeatedly(ReturnRef(route_metadata));
  EXPECT_CALL(info, route()).WillRepeatedly(Return(route));

  // Test DYNAMIC source metadata match
  {
    envoy::type::matcher::v3::MetadataMatcher matcher;
    matcher.set_filter("rbac");
    matcher.add_path()->set_key("dynamic_key");
    matcher.mutable_value()->mutable_string_match()->set_exact("dynamic_value");

    checkMatcher(MetadataMatcher(Matchers::MetadataMatcher(matcher, context),
                                 envoy::config::rbac::v3::MetadataSource::DYNAMIC),
                 true, conn, header, info);

    // Should not match with wrong value
    matcher.mutable_value()->mutable_string_match()->set_exact("wrong_value");
    checkMatcher(MetadataMatcher(Matchers::MetadataMatcher(matcher, context),
                                 envoy::config::rbac::v3::MetadataSource::DYNAMIC),
                 false, conn, header, info);
  }

  // Test ROUTE source metadata match
  {
    envoy::type::matcher::v3::MetadataMatcher matcher;
    matcher.set_filter("rbac");
    matcher.add_path()->set_key("route_key");
    matcher.mutable_value()->mutable_string_match()->set_exact("route_value");

    checkMatcher(MetadataMatcher(Matchers::MetadataMatcher(matcher, context),
                                 envoy::config::rbac::v3::MetadataSource::ROUTE),
                 true, conn, header, info);

    // Should not match with wrong value
    matcher.mutable_value()->mutable_string_match()->set_exact("wrong_value");
    checkMatcher(MetadataMatcher(Matchers::MetadataMatcher(matcher, context),
                                 envoy::config::rbac::v3::MetadataSource::ROUTE),
                 false, conn, header, info);
  }

  // Test ROUTE source with null route
  {
    EXPECT_CALL(info, route()).WillRepeatedly(Return(nullptr));

    envoy::type::matcher::v3::MetadataMatcher matcher;
    matcher.set_filter("rbac");
    matcher.add_path()->set_key("route_key");
    matcher.mutable_value()->mutable_string_match()->set_exact("route_value");

    checkMatcher(MetadataMatcher(Matchers::MetadataMatcher(matcher, context),
                                 envoy::config::rbac::v3::MetadataSource::ROUTE),
                 false, conn, header, info);
  }
}

TEST(PortRangeMatcher, TestEdgeCases) {
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;

  // Test boundary conditions
  envoy::type::v3::Int32Range range;
  range.set_start(1);
  range.set_end(65535);

  // Test port at start of range
  Envoy::Network::Address::InstanceConstSharedPtr addr1 =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 1, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr1);
  checkMatcher(PortRangeMatcher(range), true, conn, headers, info);

  // Test port at end of range - 1
  Envoy::Network::Address::InstanceConstSharedPtr addr2 =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 65534, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr2);
  checkMatcher(PortRangeMatcher(range), true, conn, headers, info);

  // Test port exactly at end (should be false as range is exclusive at end)
  Envoy::Network::Address::InstanceConstSharedPtr addr3 =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 65535, false);
  info.downstream_connection_info_provider_->setLocalAddress(addr3);
  checkMatcher(PortRangeMatcher(range), false, conn, headers, info);
}

TEST(PathMatcher, HeaderEdgeCases) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  envoy::type::matcher::v3::PathMatcher matcher;

  // Test that path matcher matches exact paths
  matcher.mutable_path()->mutable_safe_regex()->mutable_google_re2();
  matcher.mutable_path()->mutable_safe_regex()->set_regex("^/$");
  headers.setPath("/");
  checkMatcher(PathMatcher(matcher, context), true, Envoy::Network::MockConnection(), headers);

  // Test path with query parameters - regex should match path part only
  matcher.mutable_path()->mutable_safe_regex()->set_regex("^/path$");
  headers.setPath("/path?param=value");
  checkMatcher(PathMatcher(matcher, context), true, Envoy::Network::MockConnection(), headers);
  headers.setPath("/different?param=value");
  checkMatcher(PathMatcher(matcher, context), false, Envoy::Network::MockConnection(), headers);

  // Test URL encoded paths - should match the encoded form
  matcher.mutable_path()->mutable_safe_regex()->set_regex("^/path%20with%20spaces$");
  headers.setPath("/path%20with%20spaces");
  checkMatcher(PathMatcher(matcher, context), true, Envoy::Network::MockConnection(), headers);
  headers.setPath("/path with spaces"); // Not URL encoded
  checkMatcher(PathMatcher(matcher, context), false, Envoy::Network::MockConnection(), headers);
}

TEST(HeaderMatcher, MultipleHeaderValues) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::route::v3::HeaderMatcher config;
  config.set_name("multi-header");
  config.mutable_string_match()->set_exact("value1");

  Envoy::Http::TestRequestHeaderMapImpl headers;
  Envoy::Http::LowerCaseString header_name("multi-header");

  // Test single value match
  headers.setReference(header_name, "value1");
  RBAC::HeaderMatcher matcher1(config, factory_context);
  checkMatcher(matcher1, true, Envoy::Network::MockConnection(), headers);

  // Test multiple values with match present
  headers.addReference(header_name, "value2"); // Now header has both "value1" and "value2"
  config.mutable_string_match()->set_contains("value1");
  RBAC::HeaderMatcher matcher2(config, factory_context);
  checkMatcher(matcher2, true, Envoy::Network::MockConnection(), headers);

  // Test multiple values without match present
  headers.remove(header_name);
  headers.addReference(header_name, "value2");
  headers.addReference(header_name, "value3");
  config.mutable_string_match()->set_suffix("value1");
  RBAC::HeaderMatcher matcher3(config, factory_context);
  checkMatcher(matcher3, false, Envoy::Network::MockConnection(), headers);

  // Test empty value
  headers.remove(header_name);
  headers.addReference(header_name, "");
  config.mutable_string_match()->set_exact("value1");
  RBAC::HeaderMatcher matcher4(config, factory_context);
  checkMatcher(matcher4, false, Envoy::Network::MockConnection(), headers);

  // Test multiple values including empty
  headers.addReference(header_name, "value1");
  headers.addReference(header_name, "");
  config.mutable_string_match()->set_contains("value1");
  RBAC::HeaderMatcher matcher5(config, factory_context);
  checkMatcher(matcher5, true, Envoy::Network::MockConnection(), headers);
}

TEST(AuthenticatedMatcher, EmptyCertificateFields) {
  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();

  // Test with empty URI SANs but valid DNS SANs
  const std::vector<std::string> empty_uri_sans;
  const std::vector<std::string> dns_sans{"example.com"};
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(empty_uri_sans));
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  envoy::config::rbac::v3::Principal::Authenticated auth;
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  auth.mutable_principal_name()->set_exact("example.com");
  checkMatcher(AuthenticatedMatcher(auth, factory_context), true, conn);

  // Test with both empty URI and DNS SANs but valid subject
  const std::vector<std::string> empty_dns_sans;
  std::string subject = "CN=example.com";
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(empty_dns_sans));
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));

  auth.mutable_principal_name()->set_exact("CN=example.com");
  checkMatcher(AuthenticatedMatcher(auth, factory_context), true, conn);
}

TEST(MetadataMatcher, NestedMetadata) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl header;
  NiceMock<StreamInfo::MockStreamInfo> info;

  // Create nested metadata structure
  ProtobufWkt::Struct nested_struct;
  (*nested_struct.mutable_fields())["nested_key"] = ValueUtil::stringValue("nested_value");

  ProtobufWkt::Struct top_struct;
  (*top_struct.mutable_fields())["top_key"] = ValueUtil::structValue(nested_struct);

  envoy::config::core::v3::Metadata metadata;
  metadata.mutable_filter_metadata()->insert(
      Protobuf::MapPair<std::string, ProtobufWkt::Struct>("rbac", top_struct));
  EXPECT_CALL(Const(info), dynamicMetadata()).WillRepeatedly(ReturnRef(metadata));

  // Test matching nested value
  envoy::type::matcher::v3::MetadataMatcher matcher;
  matcher.set_filter("rbac");
  matcher.add_path()->set_key("top_key");
  matcher.add_path()->set_key("nested_key");
  matcher.mutable_value()->mutable_string_match()->set_exact("nested_value");

  checkMatcher(MetadataMatcher(Matchers::MetadataMatcher(matcher, context),
                               envoy::config::rbac::v3::MetadataSource::DYNAMIC),
               true, conn, header, info);

  // Test non-matching nested value
  matcher.mutable_value()->mutable_string_match()->set_exact("wrong_value");
  checkMatcher(MetadataMatcher(Matchers::MetadataMatcher(matcher, context),
                               envoy::config::rbac::v3::MetadataSource::DYNAMIC),
               false, conn, header, info);
}

TEST(PrincipalMatcher, AndIds) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Principal::Set principals;

  // Create a principal set with two conditions: any and authenticated
  auto* principal1 = principals.add_ids();
  principal1->set_any(true);

  auto* principal2 = principals.add_ids();
  auto* auth = principal2->mutable_authenticated();
  auth->mutable_principal_name()->set_exact("example.com");

  // Set up connection with SSL
  Envoy::Network::MockConnection conn;
  auto ssl = std::make_shared<Ssl::MockConnectionInfo>();

  // Set up all required SSL mock expectations
  const std::vector<std::string> uri_sans{"example.com"};
  const std::vector<std::string> dns_sans;
  const std::string subject;
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(uri_sans));
  EXPECT_CALL(*ssl, dnsSansPeerCertificate()).WillRepeatedly(Return(dns_sans));
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillRepeatedly(ReturnRef(subject));
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(ssl));

  // Should match because one URI SAN matches and any() is true
  checkMatcher(AndMatcher(principals, factory_context), true, conn);

  // Change the expected principal name to make it fail
  auth->mutable_principal_name()->set_exact("fail.com");
  checkMatcher(AndMatcher(principals, factory_context), false, conn);

  // Test with no SSL connection
  EXPECT_CALL(Const(conn), ssl()).WillRepeatedly(Return(nullptr));
  checkMatcher(AndMatcher(principals, factory_context), false, conn);
}

TEST(PrincipalMatcher, UrlPathAndFilterState) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  // Test URL path matcher
  envoy::config::rbac::v3::Principal principal_path;
  auto* path = principal_path.mutable_url_path();
  path->mutable_path()->set_exact("/test");

  Envoy::Http::TestRequestHeaderMapImpl headers;
  headers.setPath("/test");

  auto matcher_path = Matcher::create(principal_path, factory_context);
  checkMatcher(*matcher_path, true, Envoy::Network::MockConnection(), headers);

  // Test filter state matcher
  envoy::config::rbac::v3::Principal principal_filter_state;
  auto* filter_state = principal_filter_state.mutable_filter_state();
  filter_state->set_key("test.key");
  filter_state->mutable_string_match()->set_exact("test.value");

  NiceMock<StreamInfo::MockStreamInfo> info;
  StreamInfo::FilterStateImpl filter_state_impl(StreamInfo::FilterState::LifeSpan::Connection);
  filter_state_impl.setData("test.key", std::make_shared<TestObject>(),
                            StreamInfo::FilterState::StateType::ReadOnly);
  EXPECT_CALL(Const(info), filterState()).WillRepeatedly(ReturnRef(filter_state_impl));

  auto matcher_filter_state = Matcher::create(principal_filter_state, factory_context);
  checkMatcher(*matcher_filter_state, true, Envoy::Network::MockConnection(), headers, info);
}

// Test for IDENTIFIER_NOT_SET case
TEST(PrincipalMatcher, IdentifierNotSet) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Principal empty_principal;

  // This should hit the IDENTIFIER_NOT_SET case and panic
  EXPECT_DEATH(Matcher::create(empty_principal, factory_context), ".*");
}

TEST(PrincipalMatcher, OrIds) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  Protobuf::RepeatedPtrField<envoy::config::rbac::v3::Principal> principals;

  // Create two principals: one that matches direct remote IP and one that matches header
  auto* principal1 = principals.Add();
  auto* cidr = principal1->mutable_direct_remote_ip();
  cidr->set_address_prefix("1.2.3.0");
  cidr->mutable_prefix_len()->set_value(24);

  auto* principal2 = principals.Add();
  principal2->mutable_header()->set_name("test-header");
  principal2->mutable_header()->mutable_string_match()->set_exact("test-value");

  Envoy::Network::MockConnection conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;

  // Store the header name as a member to ensure it lives throughout the test
  const Envoy::Http::LowerCaseString test_header_name("test-header");

  // Set up a matching IP address
  Envoy::Network::Address::InstanceConstSharedPtr addr =
      Envoy::Network::Utility::parseInternetAddressNoThrow("1.2.3.4", 123, false);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);

  // Should match based on IP even without header
  checkMatcher(OrMatcher(principals, factory_context), true, conn, headers, info);

  // Should match based on header even with wrong IP
  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("5.6.7.8", 123, false);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);

  // Add header and verify it matches
  headers.setReference(test_header_name, "test-value");
  checkMatcher(OrMatcher(principals, factory_context), true, conn, headers, info);

  // Test with non-matching header value
  headers.remove(test_header_name);
  headers.setReference(test_header_name, "wrong-value");
  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("5.6.7.8", 123, false);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);
  checkMatcher(OrMatcher(principals, factory_context), false, conn, headers, info);
}

// Tests to cover missing lines in coverage report
TEST(IPMatcher, PrincipalSourceIpMatching) {
  // Tests lines 77-79: kSourceIp case in Principal creation
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Principal principal;
  auto* cidr = principal.mutable_source_ip();
  cidr->set_address_prefix("192.168.1.0");
  cidr->mutable_prefix_len()->set_value(24);

  auto matcher = Matcher::create(principal, factory_context);
  ASSERT_NE(matcher, nullptr);

  NiceMock<Envoy::Network::MockConnection> conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;

  // Set connection remote address that matches the CIDR
  auto addr = Envoy::Network::Utility::parseInternetAddressNoThrow("192.168.1.100", 123, false);
  conn.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr);

  EXPECT_TRUE(matcher->matches(conn, headers, info));

  // Set address that doesn't match
  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("10.0.0.1", 123, false);
  conn.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr);

  EXPECT_FALSE(matcher->matches(conn, headers, info));
}

TEST(IPMatcher, PrincipalRemoteIpMatching) {
  // Tests lines 83-85: kRemoteIp case in Principal creation
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  envoy::config::rbac::v3::Principal principal;
  auto* cidr = principal.mutable_remote_ip();
  cidr->set_address_prefix("10.0.0.0");
  cidr->mutable_prefix_len()->set_value(16);

  auto matcher = Matcher::create(principal, factory_context);
  ASSERT_NE(matcher, nullptr);

  NiceMock<Envoy::Network::MockConnection> conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;

  // Set downstream remote address that matches the CIDR
  auto addr = Envoy::Network::Utility::parseInternetAddressNoThrow("10.0.5.100", 456, false);
  info.downstream_connection_info_provider_->setRemoteAddress(addr);

  EXPECT_TRUE(matcher->matches(conn, headers, info));

  // Set address that doesn't match
  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("172.16.1.1", 456, false);
  info.downstream_connection_info_provider_->setRemoteAddress(addr);

  EXPECT_FALSE(matcher->matches(conn, headers, info));
}

TEST(IPMatcher, CreateWithInvalidCidrRange) {
  // Tests lines 206-208: Invalid CIDR range error handling in IPMatcher::create
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;

  // Add valid range first
  auto* valid_range = ranges.Add();
  valid_range->set_address_prefix("192.168.1.0");
  valid_range->mutable_prefix_len()->set_value(24);

  // Add invalid range (invalid IP address)
  auto* invalid_range = ranges.Add();
  invalid_range->set_address_prefix("invalid.ip.address");
  invalid_range->mutable_prefix_len()->set_value(24);

  auto result = IPMatcher::create(ranges, IPMatcher::Type::ConnectionRemote);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Failed to create CIDR range"));
}

TEST(IPMatcher, CreateWithEmptyRangeList) {
  // Tests empty range validation
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> empty_ranges;

  auto result = IPMatcher::create(empty_ranges, IPMatcher::Type::ConnectionRemote);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("Empty IP range list provided"));
}

TEST(IPMatcher, MatchesWithNullIpAddress) {
  // Tests line 227: null IP address check in matches method
  envoy::config::core::v3::CidrRange range;
  range.set_address_prefix("192.168.1.0");
  range.mutable_prefix_len()->set_value(24);

  auto matcher_result = IPMatcher::create(range, IPMatcher::Type::ConnectionRemote);
  ASSERT_OK(matcher_result);
  const auto& matcher = *matcher_result.value();

  NiceMock<Envoy::Network::MockConnection> conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;

  // Set null remote address
  conn.stream_info_.downstream_connection_info_provider_->setRemoteAddress(nullptr);

  EXPECT_FALSE(matcher.matches(conn, headers, info));
}

TEST(IPMatcher, MatchesWithConnectionRemoteAddress) {
  // Tests that IPMatcher correctly extracts and matches connection remote addresses.
  envoy::config::core::v3::CidrRange range;
  range.set_address_prefix("192.168.1.0");
  range.mutable_prefix_len()->set_value(24);

  // Create matcher with a specific type
  auto matcher_result = IPMatcher::create(range, IPMatcher::Type::ConnectionRemote);
  ASSERT_OK(matcher_result);
  const auto& matcher = *matcher_result.value();

  NiceMock<Envoy::Network::MockConnection> conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;

  // Set all address types to non-null to ensure we test the extraction logic
  auto addr = Envoy::Network::Utility::parseInternetAddressNoThrow("192.168.1.100", 123, false);
  conn.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr);
  info.downstream_connection_info_provider_->setLocalAddress(addr);
  info.downstream_connection_info_provider_->setDirectRemoteAddressForTest(addr);
  info.downstream_connection_info_provider_->setRemoteAddress(addr);

  // This should match and extract the connection remote address correctly
  EXPECT_TRUE(matcher.matches(conn, headers, info));
}

TEST(IPMatcher, MultipleRangesCreateSuccess) {
  // Tests successful creation with multiple ranges
  Protobuf::RepeatedPtrField<envoy::config::core::v3::CidrRange> ranges;

  // Add multiple valid ranges
  auto* range1 = ranges.Add();
  range1->set_address_prefix("192.168.1.0");
  range1->mutable_prefix_len()->set_value(24);

  auto* range2 = ranges.Add();
  range2->set_address_prefix("10.0.0.0");
  range2->mutable_prefix_len()->set_value(16);

  auto* range3 = ranges.Add();
  range3->set_address_prefix("2001:db8::");
  range3->mutable_prefix_len()->set_value(32);

  auto result = IPMatcher::create(ranges, IPMatcher::Type::ConnectionRemote);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(result.value(), nullptr);

  // Test that the created matcher works
  NiceMock<Envoy::Network::MockConnection> conn;
  Envoy::Http::TestRequestHeaderMapImpl headers;
  NiceMock<StreamInfo::MockStreamInfo> info;

  // Test IPv4 match
  auto addr = Envoy::Network::Utility::parseInternetAddressNoThrow("192.168.1.100", 123, false);
  conn.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr);
  EXPECT_TRUE(result.value()->matches(conn, headers, info));

  // Test IPv4 no match
  addr = Envoy::Network::Utility::parseInternetAddressNoThrow("172.16.1.1", 123, false);
  conn.stream_info_.downstream_connection_info_provider_->setRemoteAddress(addr);
  EXPECT_FALSE(result.value()->matches(conn, headers, info));
}

// Tests for kDestinationIp case in Permission matcher creation.
TEST(Matcher, CreatePermissionDestinationIp) {
  envoy::config::rbac::v3::Permission permission;
  auto* cidr = permission.mutable_destination_ip();
  cidr->set_address_prefix("192.168.1.0");
  cidr->mutable_prefix_len()->set_value(24);

  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto matcher = Matcher::create(permission, validation_visitor, context);
  EXPECT_NE(matcher, nullptr);
}

// Tests error handling in kDestinationIp case with invalid CIDR.
TEST(Matcher, CreatePermissionDestinationIpInvalidCidr) {
  envoy::config::rbac::v3::Permission permission;
  auto* cidr = permission.mutable_destination_ip();
  cidr->set_address_prefix("invalid.ip.address");
  cidr->mutable_prefix_len()->set_value(24);

  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_THROW_WITH_REGEX(Matcher::create(permission, validation_visitor, context), EnvoyException,
                          "Failed to create CIDR range:.*malformed IP address");
}

// Tests for RULE_NOT_SET case that falls through to PANIC.
TEST(Matcher, CreatePermissionRuleNotSet) {
  EXPECT_DEATH(
      {
        envoy::config::rbac::v3::Permission permission;

        NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor;
        NiceMock<Server::Configuration::MockServerFactoryContext> context;

        Matcher::create(permission, validation_visitor, context);
      },
      "panic: corrupted enum");
}

// Tests for kSourceIp case in Principal matcher creation.
TEST(Matcher, CreatePrincipalSourceIp) {
  envoy::config::rbac::v3::Principal principal;
  auto* cidr = principal.mutable_source_ip();
  cidr->set_address_prefix("10.0.0.0");
  cidr->mutable_prefix_len()->set_value(16);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto matcher = Matcher::create(principal, context);
  EXPECT_NE(matcher, nullptr);
}

// Tests error handling in kSourceIp case with invalid CIDR.
TEST(Matcher, CreatePrincipalSourceIpInvalidCidr) {
  envoy::config::rbac::v3::Principal principal;
  auto* cidr = principal.mutable_source_ip();
  cidr->set_address_prefix("999.999.999.999");
  cidr->mutable_prefix_len()->set_value(24);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_THROW_WITH_REGEX(Matcher::create(principal, context), EnvoyException,
                          "Failed to create CIDR range:.*malformed IP address");
}

// Tests for kDirectRemoteIp case in Principal matcher creation.
TEST(Matcher, CreatePrincipalDirectRemoteIp) {
  envoy::config::rbac::v3::Principal principal;
  auto* cidr = principal.mutable_direct_remote_ip();
  cidr->set_address_prefix("172.16.0.0");
  cidr->mutable_prefix_len()->set_value(12);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto matcher = Matcher::create(principal, context);
  EXPECT_NE(matcher, nullptr);
}

// Tests error handling in kDirectRemoteIp case with invalid CIDR.
TEST(Matcher, CreatePrincipalDirectRemoteIpInvalidCidr) {
  envoy::config::rbac::v3::Principal principal;
  auto* cidr = principal.mutable_direct_remote_ip();
  cidr->set_address_prefix(""); // Empty IP address
  cidr->mutable_prefix_len()->set_value(24);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_THROW_WITH_REGEX(Matcher::create(principal, context), EnvoyException,
                          "Failed to create CIDR range:.*malformed IP address");
}

// Tests for kRemoteIp case in Principal matcher creation.
TEST(Matcher, CreatePrincipalRemoteIp) {
  envoy::config::rbac::v3::Principal principal;
  auto* cidr = principal.mutable_remote_ip();
  cidr->set_address_prefix("2001:db8::");
  cidr->mutable_prefix_len()->set_value(32);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  auto matcher = Matcher::create(principal, context);
  EXPECT_NE(matcher, nullptr);
}

// Tests error handling in kRemoteIp case with invalid CIDR.
TEST(Matcher, CreatePrincipalRemoteIpInvalidCidr) {
  envoy::config::rbac::v3::Principal principal;
  auto* cidr = principal.mutable_remote_ip();
  cidr->set_address_prefix("2001:db8::gggg"); // Invalid IPv6
  cidr->mutable_prefix_len()->set_value(32);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  EXPECT_THROW_WITH_REGEX(Matcher::create(principal, context), EnvoyException,
                          "Failed to create CIDR range:.*malformed IP address");
}

// Tests for IDENTIFIER_NOT_SET case that falls through to PANIC.
TEST(Matcher, CreatePrincipalIdentifierNotSet) {
  EXPECT_DEATH(
      {
        envoy::config::rbac::v3::Principal principal;

        NiceMock<Server::Configuration::MockServerFactoryContext> context;

        Matcher::create(principal, context);
      },
      "panic: corrupted enum");
}

} // namespace
} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
