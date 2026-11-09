#include "source/extensions/filters/common/rbac/engine_impl.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/utility.h"

#include "contrib/postgres_proxy/filters/network/source/postgres_filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {
namespace {

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using namespace std::literals::string_literals;

class PostgresRbacTest : public testing::Test {
public:
  PostgresRbacTest() {
    PostgresFilterConfig::PostgresFilterConfigOptions options{
        "test.", false, false,
        envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::DISABLE,
        envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::DISABLE};
    config_ = std::make_shared<PostgresFilterConfig>(options, *store_.rootScope());
    filter_ = std::make_unique<PostgresFilter>(config_);
    EXPECT_CALL(read_, connection()).WillRepeatedly(ReturnRef(connection_));
    EXPECT_CALL(connection_, streamInfo()).WillRepeatedly(ReturnRef(info_));
    ON_CALL(info_, setDynamicMetadata(_, _))
        .WillByDefault(Invoke([this](const std::string& name, const Protobuf::Struct& value) {
          (*info_.metadata_.mutable_filter_metadata())[name].MergeFrom(value);
        }));
    ON_CALL(connection_, ssl()).WillByDefault(Return(nullptr));
    filter_->initializeReadFilterCallbacks(read_);
    filter_->initializeWriteFilterCallbacks(write_);
  }

  void rules(const std::string& yaml) {
    envoy::config::rbac::v3::RBAC rules;
    TestUtility::loadFromYaml(yaml, rules);
    config_->engine_ = std::make_unique<Filters::Common::RBAC::RoleBasedAccessControlEngineImpl>(
        rules, ProtobufMessage::getStrictValidationVisitor(), context_);
  }

  std::string startup(const std::string& attributes = "user\0postgres\0database\0testdb\0\0"s) {
    Buffer::OwnedImpl body;
    body.writeBEInt<uint32_t>(8 + attributes.size());
    body.writeBEInt<uint32_t>(0x00030000);
    body.add(attributes);
    return body.toString();
  }

  std::string cancel(uint32_t key, uint32_t length = 16) {
    Buffer::OwnedImpl body;
    body.writeBEInt<uint32_t>(length);
    body.writeBEInt<uint32_t>(80877102);
    body.writeBEInt<uint32_t>(0x11223344);
    body.writeBEInt<uint32_t>(key);
    return body.toString();
  }

  void expectDenied(absl::string_view details = "rbac_access_denied_matched_policy[none]") {
    EXPECT_CALL(info_, setConnectionTerminationDetails(details));
    EXPECT_CALL(read_, injectReadDataToFilterChain(_, _)).Times(0);
    EXPECT_CALL(write_, injectWriteDataToFilterChain(_, false))
        .WillOnce(Invoke([](Buffer::Instance& data, bool) {
          ASSERT_THAT(data.peekBEInt<char>(0), 'E');
          ASSERT_THAT(data.toString(), testing::HasSubstr("28000"));
        }));
    EXPECT_CALL(connection_, close(Network::ConnectionCloseType::FlushWrite));
  }

  const Protobuf::Struct& metadata() {
    return info_.metadata_.filter_metadata().at(NetworkFilterNames::get().PostgresProxy);
  }

  Stats::IsolatedStoreImpl store_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  testing::StrictMock<Network::MockReadFilterCallbacks> read_;
  testing::StrictMock<Network::MockWriteFilterCallbacks> write_;
  testing::NiceMock<Network::MockConnection> connection_;
  testing::NiceMock<StreamInfo::MockStreamInfo> info_;
  PostgresFilterConfigSharedPtr config_;
  std::unique_ptr<PostgresFilter> filter_;
};

constexpr char Rules[] = R"EOF(
action: ALLOW
policies:
  foo:
    principals:
    - any: true
    permissions:
    - and_rules:
        rules:
        - metadata:
            filter: envoy.filters.network.postgres_proxy
            path: [{key: user}]
            value: {string_match: {exact: postgres}}
        - metadata:
            filter: envoy.filters.network.postgres_proxy
            path: [{key: database}]
            value: {string_match: {exact: testdb}}
)EOF";

TEST_F(PostgresRbacTest, AllowsStartup) {
  rules(Rules);
  const std::string packet =
      startup("user\0postgres\0database\0testdb\0application_name\0psql\0\0"s);
  Buffer::OwnedImpl data(packet);
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::Continue);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 1);
  ASSERT_THAT(metadata().fields().at("user").string_value(), "postgres");
  ASSERT_THAT(metadata().fields().at("database").string_value(), "testdb");
  ASSERT_THAT(metadata().fields().at("application_name").string_value(), "psql");
}

class PostgresRbacEffectiveNamesTest
    : public PostgresRbacTest,
      public testing::WithParamInterface<std::tuple<std::string, std::string, std::string>> {
public:
  void deniesEffectiveName(const std::string& attribute) {
    const auto& [attributes, user, database] = GetParam();
    const std::string& name = attribute == "user" ? user : database;
    rules("action: DENY\npolicies:\n  blocked:\n"
          "    principals: [{any: true}]\n    permissions:\n    - metadata:\n"
          "        filter: envoy.filters.network.postgres_proxy\n"
          "        path: [{key: " +
          attribute +
          "}]\n"
          "        value: {string_match: {exact: \"" +
          name + "\"}}\n");
    expectDenied("rbac_access_denied_matched_policy[blocked]");
    Buffer::OwnedImpl data(startup(attributes));
    EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
    EXPECT_EQ(data.length(), 0);
    EXPECT_EQ(metadata().fields().at("user").string_value(), user);
    EXPECT_EQ(metadata().fields().at("database").string_value(), database);
    EXPECT_EQ(config_->stats_.authorization_denied_.value(), 1);
    EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
  }
};

TEST_P(PostgresRbacEffectiveNamesTest, DeniesEffectiveUser) { deniesEffectiveName("user"); }

TEST_P(PostgresRbacEffectiveNamesTest, DeniesEffectiveDatabase) { deniesEffectiveName("database"); }

INSTANTIATE_TEST_SUITE_P(
    StartupNames, PostgresRbacEffectiveNamesTest,
    testing::Values(std::make_tuple("user\0postgres\0database\0\0\0"s, "postgres", "postgres"),
                    std::make_tuple("user\0postgres\0\0"s, "postgres", "postgres"),
                    std::make_tuple("user\0"s + std::string(64, 'u') + "\0\0"s,
                                    std::string(63, 'u'), std::string(63, 'u')),
                    std::make_tuple("user\0"s + std::string(64, 'u') + "\0database\0\0\0"s,
                                    std::string(63, 'u'), std::string(63, 'u')),
                    std::make_tuple("user\0"s + std::string(64, 'u') + "\0database\0"s +
                                        std::string(64, 'd') + "\0\0"s,
                                    std::string(63, 'u'), std::string(63, 'd')),
                    std::make_tuple("user\0"s + std::string(63, 'u') + "\0database\0"s +
                                        std::string(63, 'd') + "\0\0"s,
                                    std::string(63, 'u'), std::string(63, 'd'))));

class PostgresRbacCancelTest : public PostgresRbacTest,
                               public testing::WithParamInterface<uint32_t> {};

TEST_P(PostgresRbacCancelTest, AllowsUsingConnectionIdentity) {
  rules(R"EOF(
action: ALLOW
policies:
  client:
    permissions: [{any: true}]
    principals:
    - authenticated:
        principal_name: {exact: "spiffe://example.com/app"}
)EOF");
  auto ssl = std::make_shared<testing::NiceMock<Ssl::MockConnectionInfo>>();
  const std::vector<std::string> sans{"spiffe://example.com/app"};
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(absl::MakeConstSpan(sans)));
  const std::string packet = cancel(GetParam());
  Buffer::OwnedImpl data(packet);
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), packet);
  EXPECT_TRUE(metadata().fields().empty());
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
  EXPECT_EQ(config_->stats_.messages_frontend_.value(), 1);
  EXPECT_EQ(config_->stats_.sessions_encrypted_.value(), 0);
}

TEST_P(PostgresRbacCancelTest, DeniesWhenStartupAttributesAreRequired) {
  rules(Rules);
  expectDenied();
  Buffer::OwnedImpl data(cancel(GetParam()));
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_TRUE(metadata().fields().empty());
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 1);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
}

TEST_P(PostgresRbacCancelTest, DeniesMatchingConnectionRule) {
  rules("action: DENY\npolicies:\n  blocked:\n"
        "    principals: [{any: true}]\n    permissions: [{any: true}]\n");
  expectDenied("rbac_access_denied_matched_policy[blocked]");
  Buffer::OwnedImpl data(cancel(GetParam()));
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_TRUE(metadata().fields().empty());
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 1);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
}

TEST_P(PostgresRbacCancelTest, PassesThroughWithoutRbac) {
  EXPECT_CALL(info_, setDynamicMetadata(_, _)).Times(0);
  const std::string packet = cancel(GetParam());
  Buffer::OwnedImpl data(packet);
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), packet);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
}

INSTANTIATE_TEST_SUITE_P(CancelSecretKeys, PostgresRbacCancelTest,
                         testing::Values(0x11223300, 0x11223344));

class PostgresRbacSplitCancelTest : public PostgresRbacTest,
                                    public testing::WithParamInterface<size_t> {};

TEST_P(PostgresRbacSplitCancelTest, AuthorizesOnlyTheCompletePacket) {
  rules("action: DENY\npolicies: {}\n");
  const std::string packet = cancel(0x11223344);
  Buffer::OwnedImpl data(packet.substr(0, GetParam()));
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
  EXPECT_TRUE(info_.metadata_.filter_metadata().empty());

  data.add(packet.substr(GetParam()));
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), packet);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
  EXPECT_TRUE(metadata().fields().empty());
}

INSTANTIATE_TEST_SUITE_P(CancelFragments, PostgresRbacSplitCancelTest,
                         testing::Values(1, 4, 7, 8, 12, 15));

class PostgresRbacInvalidCancelTest : public PostgresRbacTest,
                                      public testing::WithParamInterface<uint32_t> {};

TEST_P(PostgresRbacInvalidCancelTest, RejectsInvalidLength) {
  rules("action: DENY\npolicies: {}\n");
  EXPECT_CALL(info_, setDynamicMetadata(_, _)).Times(0);
  expectDenied();
  Buffer::OwnedImpl data(cancel(0x11223300, GetParam()));
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 1);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
}

INSTANTIATE_TEST_SUITE_P(CancelLengths, PostgresRbacInvalidCancelTest,
                         testing::Values(8, 12, 15, 17));

class PostgresRbacSplitStartupTest : public PostgresRbacTest,
                                     public testing::WithParamInterface<size_t> {};

TEST_P(PostgresRbacSplitStartupTest, ForwardsExactlyTheAuthorizedStartup) {
  rules(Rules);
  const std::string packet = startup();
  Buffer::OwnedImpl data(packet.substr(0, GetParam()));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);

  data.add(packet.substr(GetParam()));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), packet);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
  EXPECT_EQ(metadata().fields().at("user").string_value(), "postgres");
  EXPECT_EQ(metadata().fields().at("database").string_value(), "testdb");
}

INSTANTIATE_TEST_SUITE_P(StartupFragments, PostgresRbacSplitStartupTest,
                         testing::Values(1, 4, 7, 8, 15, 36));

TEST_F(PostgresRbacTest, DoesNotForwardOnlyAnEmbeddedStartup) {
  rules(Rules);
  const std::string embedded = startup("user\0blocked\0\0"s);
  const std::string packet = startup("user\0postgres\0database\0testdb\0\0"s + embedded);
  Buffer::OwnedImpl data(packet.substr(0, packet.size() - embedded.size()));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);

  data.add(embedded);
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(metadata().fields().at("user").string_value(), "postgres");
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
  EXPECT_EQ(data.toString(), packet);
}

TEST_F(PostgresRbacTest, DiscardsAllFragmentsOnDenial) {
  rules(Rules);
  expectDenied();
  const std::string packet = startup("user\0blocked\0\0"s);
  Buffer::OwnedImpl data(packet.substr(0, 4));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);

  data.add(packet.substr(4));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  data.add(startup());
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 1);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
}

TEST_F(PostgresRbacTest, RejectsPlaintextBeforeAuthorization) {
  rules(Rules);
  config_->downstream_ssl_ =
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::REQUIRE;
  config_->upstream_ssl_ =
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::REQUIRE;
  EXPECT_CALL(info_, setDynamicMetadata(_, _)).Times(0);
  EXPECT_CALL(read_, injectReadDataToFilterChain(_, _)).Times(0);
  EXPECT_CALL(write_, injectWriteDataToFilterChain(_, false))
      .WillOnce(
          Invoke([](Buffer::Instance& data, bool) { EXPECT_EQ(data.peekBEInt<char>(0), 'E'); }));
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));

  Buffer::OwnedImpl data(startup());
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  data.add(startup());
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
}

TEST_F(PostgresRbacTest, AuthorizesAfterSplitDownstreamSSL) {
  rules(Rules);
  config_->downstream_ssl_ =
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::REQUIRE;
  Network::Connection::BytesSentCb sent;
  EXPECT_CALL(connection_, addBytesSentCallback(_)).WillOnce(testing::SaveArg<0>(&sent));
  EXPECT_CALL(write_, injectWriteDataToFilterChain(_, false))
      .WillOnce(Invoke([](Buffer::Instance& data, bool) { EXPECT_EQ(data.toString(), "S"); }));
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(8);
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  data.writeBEInt<uint32_t>(80877103);
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);

  EXPECT_CALL(connection_, startSecureTransport()).WillOnce(Return(true));
  sent(1);
  const std::string packet = startup();
  data.add(packet.substr(0, 3));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  data.add(packet.substr(3));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), packet);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
  EXPECT_EQ(metadata().fields().at("user").string_value(), "postgres");
}

TEST_F(PostgresRbacTest, RejectsInvalidStartupWithRbac) {
  rules("action: DENY\npolicies: {}\n");
  EXPECT_CALL(info_, setDynamicMetadata(_, _)).Times(0);
  expectDenied();

  // The complete packet has an unterminated attribute value.
  Buffer::OwnedImpl data(startup("user\0postgres"s));
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  ASSERT_THAT(data.length(), 0);

  data.add(startup());
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  ASSERT_THAT(data.length(), 0);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 0);
  ASSERT_THAT(config_->stats_.authorization_denied_.value(), 1);
}

class PostgresRbacStartupLengthTest : public PostgresRbacTest,
                                      public testing::WithParamInterface<uint32_t> {
public:
  std::string packet(const std::string& user) {
    const std::string attributes = "user\0"s + user + "\0database\0testdb\0application_name\0"s;
    return startup(attributes + std::string(GetParam() - 8 - attributes.size() - 2, 'a') + "\0\0"s);
  }
};

TEST_P(PostgresRbacStartupLengthTest, AllowsAuthorizedStartup) {
  rules(Rules);
  const std::string bytes = packet("postgres");
  ASSERT_EQ(bytes.size(), GetParam());
  Buffer::OwnedImpl data(bytes);
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), bytes);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
  EXPECT_EQ(metadata().fields().at("user").string_value(), "postgres");
}

TEST_P(PostgresRbacStartupLengthTest, DeniesUnauthorizedSplitStartup) {
  rules(Rules);
  expectDenied();
  const std::string bytes = packet("blocked");
  ASSERT_EQ(bytes.size(), GetParam());
  Buffer::OwnedImpl data(bytes.substr(0, 4));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);

  data.add(bytes.substr(4));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 1);
  EXPECT_EQ(metadata().fields().at("user").string_value(), "blocked");
}

// Keep the decoder's existing 10000-byte limit.
INSTANTIATE_TEST_SUITE_P(StartupLengthBoundary, PostgresRbacStartupLengthTest,
                         testing::Values(9999, 10000));

class PostgresOversizedStartupTest : public PostgresRbacTest,
                                     public testing::WithParamInterface<uint32_t> {};

TEST_P(PostgresOversizedStartupTest, RejectsWithRbac) {
  rules("action: DENY\npolicies: {}\n");
  EXPECT_CALL(info_, setDynamicMetadata(_, _)).Times(0);
  expectDenied();

  // A length over the decoder's limit must be rejected without waiting for the body.
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(GetParam());
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  data.add(startup());
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 1);
}

TEST_P(PostgresOversizedStartupTest, PreservesPassthroughWithoutRbac) {
  EXPECT_CALL(info_, setDynamicMetadata(_, _)).Times(0);
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(GetParam());
  const std::string bytes = data.toString();
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), bytes);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
}

INSTANTIATE_TEST_SUITE_P(OversizedStartups, PostgresOversizedStartupTest,
                         testing::Values(10001, 10002, 10003, 10004, 10005, 0xffffffff));

TEST_F(PostgresRbacTest, DeniesBeforeUpstreamSSL) {
  rules(Rules);
  config_->upstream_ssl_ =
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::REQUIRE;
  expectDenied();
  Buffer::OwnedImpl data(startup("user\0app\0database\0other\0\0"s));
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  ASSERT_THAT(data.length(), 0);
  data.add(startup());
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  ASSERT_THAT(data.length(), 0);
  ASSERT_THAT(config_->stats_.authorization_denied_.value(), 1);
}

TEST_F(PostgresRbacTest, EmptyRulesDeny) {
  rules("{}");
  expectDenied();
  Buffer::OwnedImpl data(startup());
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
}

TEST_F(PostgresRbacTest, DenyRulesAllowUnmatchedConnections) {
  rules("action: DENY\npolicies: {}\n");
  Buffer::OwnedImpl data(startup("user\0app\0\0"s));
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::Continue);
  ASSERT_THAT(metadata().fields().at("database").string_value(), "app");
}

TEST_F(PostgresRbacTest, LogRulesAllow) {
  rules("action: LOG\npolicies: {}\n");
  Buffer::OwnedImpl data(startup());
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::Continue);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 1);
}

TEST_F(PostgresRbacTest, MatchesPeerUriSan) {
  rules(R"EOF(
action: ALLOW
policies:
  client:
    permissions: [{any: true}]
    principals:
    - authenticated:
        principal_name: {exact: "spiffe://example.com/app"}
)EOF");
  auto ssl = std::make_shared<testing::NiceMock<Ssl::MockConnectionInfo>>();
  std::vector<std::string> sans{"spiffe://example.com/app"};
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  EXPECT_CALL(*ssl, uriSanPeerCertificate()).WillRepeatedly(Return(absl::MakeConstSpan(sans)));
  Buffer::OwnedImpl data(startup());
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::Continue);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 1);
}

TEST_F(PostgresRbacTest, MissingPeerIdentityDenied) {
  rules(R"EOF(
action: ALLOW
policies:
  client:
    permissions: [{any: true}]
    principals:
    - authenticated:
        principal_name: {exact: "spiffe://example.com/app"}
)EOF");
  auto ssl = std::make_shared<testing::NiceMock<Ssl::MockConnectionInfo>>();
  ON_CALL(connection_, ssl()).WillByDefault(Return(ssl));
  const std::string empty_subject;
  EXPECT_CALL(*ssl, subjectPeerCertificate()).WillOnce(ReturnRef(empty_subject));
  expectDenied();
  Buffer::OwnedImpl data(startup());
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
}

TEST_F(PostgresRbacTest, DenyRuleRecordsMatchedPolicyID) {
  rules(R"EOF(
action: DENY
policies:
  "blocked client":
    principals: [{any: true}]
    permissions: [{any: true}]
)EOF");
  expectDenied("rbac_access_denied_matched_policy[blocked_client]");
  Buffer::OwnedImpl data(startup());
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  ASSERT_THAT(data.length(), 0);
  ASSERT_THAT(config_->stats_.authorization_denied_.value(), 1);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 0);
}

TEST_F(PostgresRbacTest, DenySSLPassthrough) {
  rules("{}");
  // deny all rules should reject ssl passthrough conns
  expectDenied();
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(8);
  data.writeBEInt<uint32_t>(80877103);
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  ASSERT_THAT(data.length(), 0);
  ASSERT_THAT(config_->stats_.authorization_denied_.value(), 1);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 0);
}

TEST_F(PostgresRbacTest, AllowSSLPassthrough) {
  rules("action: LOG\npolicies: {}\n");

  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(8);
  data.writeBEInt<uint32_t>(80877103);

  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::Continue);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 1);
  ASSERT_THAT(config_->stats_.authorization_denied_.value(), 0);
}

TEST_F(PostgresRbacTest, EmptyStartupValueDoesNotBypassUserDenyRule) {
  rules(R"EOF(
action: DENY
policies:
  blocked_user:
    principals: [{any: true}]
    permissions:
    - metadata:
        filter: envoy.filters.network.postgres_proxy
        path: [{key: user}]
        value: {string_match: {exact: blocked}}
)EOF");
  expectDenied("rbac_access_denied_matched_policy[blocked_user]");
  Buffer::OwnedImpl data(startup("application_name\0\0user\0blocked\0database\0testdb\0\0"s));
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  ASSERT_THAT(data.length(), 0);
  ASSERT_THAT(metadata().fields().at("user").string_value(), "blocked");
  ASSERT_THAT(config_->stats_.authorization_denied_.value(), 1);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 0);
}

} // namespace
} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
