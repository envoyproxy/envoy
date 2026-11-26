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
    config_->authorization_enforced_ = rules.action() != envoy::config::rbac::v3::RBAC::LOG;
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

  std::string cancelWithKey(absl::string_view key) {
    Buffer::OwnedImpl body;
    body.writeBEInt<uint32_t>(Postgres::Protocol::CANCEL_REQUEST_HEADER_SIZE + key.size());
    body.writeBEInt<uint32_t>(Postgres::Protocol::CANCEL_REQUEST_CODE);
    body.writeBEInt<uint32_t>(0x11223344);
    body.add(key);
    return body.toString();
  }

  std::string cancel(uint32_t key) {
    Buffer::OwnedImpl body;
    body.writeBEInt<uint32_t>(key);
    return cancelWithKey(body.toString());
  }

  std::string encryptionRequest(uint32_t code) {
    Buffer::OwnedImpl body;
    body.writeBEInt<uint32_t>(Postgres::Protocol::STARTUP_HEADER_SIZE);
    body.writeBEInt<uint32_t>(code);
    return body.toString();
  }

  void expectStartupError(absl::string_view details, absl::string_view sqlstate) {
    EXPECT_CALL(info_, setConnectionTerminationDetails(details));
    EXPECT_CALL(read_, injectReadDataToFilterChain(_, _)).Times(0);
    EXPECT_CALL(write_, injectWriteDataToFilterChain(_, false))
        .WillOnce(Invoke([sqlstate](Buffer::Instance& data, bool) {
          ASSERT_THAT(data.peekBEInt<char>(0), 'E');
          ASSERT_THAT(data.toString(), testing::HasSubstr(sqlstate));
        }));
    EXPECT_CALL(connection_, close(Network::ConnectionCloseType::FlushWrite));
  }

  void expectDenied(absl::string_view details = "rbac_access_denied_matched_policy[none]") {
    expectStartupError(details, "28000");
  }

  const Protobuf::Struct& metadata() {
    return info_.metadata_.filter_metadata().at(NetworkFilterNames::get().PostgresProxy);
  }

  std::string packetType() {
    return info_.metadata_.filter_metadata()
        .at("envoy.filters.network.postgres_proxy.rbac")
        .fields()
        .at("packet_type")
        .string_value();
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

constexpr char CancelOnlyRules[] = R"EOF(
action: ALLOW
policies:
  cancel:
    principals: [{any: true}]
    permissions:
    - metadata:
        filter: envoy.filters.network.postgres_proxy.rbac
        path: [{key: packet_type}]
        value: {string_match: {exact: cancel_request}}
)EOF";

constexpr char DenyBlockedUser[] = R"EOF(
action: DENY
policies:
  blocked_user:
    principals: [{any: true}]
    permissions:
    - metadata:
        filter: envoy.filters.network.postgres_proxy
        path: [{key: user}]
        value: {string_match: {exact: blocked}}
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
  rules(CancelOnlyRules);
  const std::string packet = cancelWithKey(std::string(256, 'k'));
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
  EXPECT_EQ(packetType(), "cancel_request");
}

INSTANTIATE_TEST_SUITE_P(CancelFragments, PostgresRbacSplitCancelTest,
                         testing::Values(1, 4, 7, 8, 12, 15, 267));

TEST_F(PostgresRbacTest, AllowsOneByteCancelKeyByPacketType) {
  rules(CancelOnlyRules);
  const std::string packet = cancelWithKey("k");
  Buffer::OwnedImpl data(packet);
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), packet);
  EXPECT_EQ(packetType(), "cancel_request");
}

TEST_F(PostgresRbacTest, StartupParameterCannotMatchCancelPolicy) {
  rules(CancelOnlyRules);
  expectDenied();
  Buffer::OwnedImpl data(startup("user\0postgres\0packet_type\0cancel_request\0\0"s));
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(packetType(), "startup_message");
}

TEST_F(PostgresRbacTest, SSLRequestDoesNotMatchCancelPolicy) {
  rules(CancelOnlyRules);
  expectDenied();
  Buffer::OwnedImpl data(encryptionRequest(Postgres::Protocol::SSL_REQUEST_CODE));
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(packetType(), "ssl_request");
}

class PostgresRbacInvalidCancelTest : public PostgresRbacTest,
                                      public testing::WithParamInterface<uint32_t> {};

TEST_P(PostgresRbacInvalidCancelTest, RejectsInvalidLength) {
  rules("action: DENY\npolicies: {}\n");
  EXPECT_CALL(info_, setDynamicMetadata(_, _)).Times(0);
  expectStartupError("postgres_proxy_invalid_startup", "08P01");
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(GetParam());
  data.writeBEInt<uint32_t>(Postgres::Protocol::CANCEL_REQUEST_CODE);
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
  EXPECT_EQ(config_->stats_.protocol_rejected_.value(), 1);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
}

INSTANTIATE_TEST_SUITE_P(CancelLengths, PostgresRbacInvalidCancelTest, testing::Values(8, 12, 269));

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
  expectStartupError("postgres_proxy_invalid_startup", "08P01");

  // The complete packet has an unterminated attribute value.
  Buffer::OwnedImpl data(startup("user\0postgres"s));
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  ASSERT_THAT(data.length(), 0);

  data.add(startup());
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  ASSERT_THAT(data.length(), 0);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 0);
  ASSERT_THAT(config_->stats_.authorization_denied_.value(), 0);
  EXPECT_EQ(config_->stats_.protocol_rejected_.value(), 1);
}

class PostgresUndersizedStartupTest : public PostgresRbacTest,
                                      public testing::WithParamInterface<uint32_t> {};

TEST_P(PostgresUndersizedStartupTest, RejectsWithEnforcingRules) {
  rules("action: DENY\npolicies: {}\n");
  EXPECT_CALL(info_, setDynamicMetadata(_, _)).Times(0);
  expectStartupError("postgres_proxy_invalid_startup", "08P01");

  // A declared length shorter than the header needs no additional bytes to reject.
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(GetParam());
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(filter_->getFrontendBufLength(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
  EXPECT_EQ(config_->stats_.protocol_rejected_.value(), 1);
}

INSTANTIATE_TEST_SUITE_P(UndersizedStartups, PostgresUndersizedStartupTest,
                         testing::Values(0, 3, 4, 7));

class PostgresNonEnforcingStartupTest : public PostgresRbacTest,
                                        public testing::WithParamInterface<bool> {};

TEST_P(PostgresNonEnforcingStartupTest, ForwardsUndersizedStartup) {
  if (GetParam()) {
    rules("action: LOG\npolicies: {}\n");
  }
  EXPECT_CALL(connection_, close(_)).Times(0);
  EXPECT_CALL(info_, setDynamicMetadata(_, _)).Times(0);
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(7);
  const std::string original = data.toString();
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), original);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
}

INSTANTIATE_TEST_SUITE_P(LoggingAndNoRules, PostgresNonEnforcingStartupTest, testing::Bool());

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
  expectStartupError("postgres_proxy_invalid_startup", "08P01");

  // A length over the decoder's limit must be rejected without waiting for the body.
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(GetParam());
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  data.add(startup());
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
  EXPECT_EQ(config_->stats_.protocol_rejected_.value(), 1);
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

TEST_F(PostgresRbacTest, RejectsClientDataWhileNegotiatingUpstreamSSL) {
  rules(Rules);
  config_->upstream_ssl_ =
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::REQUIRE;
  const std::string request = encryptionRequest(Postgres::Protocol::SSL_REQUEST_CODE);
  EXPECT_CALL(read_, injectReadDataToFilterChain(_, false))
      .WillOnce(Invoke([&request](Buffer::Instance& data, bool) {
        EXPECT_EQ(data.toString(), request);
        data.drain(data.length());
      }));
  EXPECT_CALL(read_, startUpstreamSecureTransport()).Times(0);

  Buffer::OwnedImpl data(startup());
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  Buffer::OwnedImpl early("unexpected client data");
  EXPECT_EQ(filter_->onData(early, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(early.length(), 0);

  Buffer::OwnedImpl reply("S");
  EXPECT_EQ(filter_->onWrite(reply, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(reply.length(), 0);
}

class PostgresRbacCancelUpstreamSSLTest : public PostgresRbacTest,
                                          public testing::WithParamInterface<bool> {};

TEST_P(PostgresRbacCancelUpstreamSSLTest, ForwardsCancelOnlyAfterStartingUpstreamSSL) {
  rules(CancelOnlyRules);
  config_->upstream_ssl_ =
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::REQUIRE;
  // Legacy clients send cancellation on a separate plaintext connection.
  config_->downstream_ssl_ =
      envoy::extensions::filters::network::postgres_proxy::v3alpha::PostgresProxy::REQUIRE;
  const std::string request = encryptionRequest(Postgres::Protocol::SSL_REQUEST_CODE);
  const std::string packet = cancel(0x11223344);
  {
    testing::InSequence sequence;
    EXPECT_CALL(read_, injectReadDataToFilterChain(_, false))
        .WillOnce(Invoke([&request](Buffer::Instance& data, bool) {
          EXPECT_EQ(data.toString(), request);
          data.drain(data.length());
        }));
    if (GetParam()) {
      EXPECT_CALL(read_, startUpstreamSecureTransport()).WillOnce(Return(true));
      EXPECT_CALL(read_, injectReadDataToFilterChain(_, false))
          .WillOnce(Invoke([&packet](Buffer::Instance& data, bool) {
            EXPECT_EQ(data.toString(), packet);
            data.drain(data.length());
          }));
    } else {
      EXPECT_CALL(read_, startUpstreamSecureTransport()).Times(0);
      EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
    }
  }

  Buffer::OwnedImpl data(packet);
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(packetType(), "cancel_request");
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);

  Buffer::OwnedImpl reply(GetParam() ? "S" : "N");
  EXPECT_EQ(filter_->onWrite(reply, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(reply.length(), 0);
  EXPECT_EQ(config_->stats_.sessions_upstream_ssl_success_.value(), GetParam() ? 1 : 0);
  EXPECT_EQ(config_->stats_.sessions_upstream_ssl_failed_.value(), GetParam() ? 0 : 1);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
}

INSTANTIATE_TEST_SUITE_P(CancelUpstreamSSL, PostgresRbacCancelUpstreamSSLTest, testing::Bool());

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

TEST_F(PostgresRbacTest, EnforcedSSLPassthroughRelaysAccept) {
  rules("action: DENY\npolicies: {}\n");
  const std::string request = encryptionRequest(Postgres::Protocol::SSL_REQUEST_CODE);
  Buffer::OwnedImpl data(request.substr(0, 6));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  data.add(request.substr(6));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), request);
  EXPECT_EQ(packetType(), "ssl_request");
  EXPECT_EQ(config_->stats_.sessions_encrypted_.value(), 0);

  Buffer::OwnedImpl reply("S");
  EXPECT_EQ(filter_->onWrite(reply, false), Network::FilterStatus::Continue);
  EXPECT_EQ(reply.toString(), "S");
  EXPECT_EQ(config_->stats_.sessions_encrypted_.value(), 1);
  Buffer::OwnedImpl encrypted("opaque TLS bytes");
  EXPECT_EQ(filter_->onData(encrypted, false), Network::FilterStatus::Continue);
  EXPECT_EQ(encrypted.toString(), "opaque TLS bytes");
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
}

class PostgresRbacSSLFallbackTest : public PostgresRbacTest,
                                    public testing::WithParamInterface<bool> {};

TEST_P(PostgresRbacSSLFallbackTest, AuthorizesPlaintextStartupAfterServerDeclinesSSL) {
  rules(DenyBlockedUser);
  Buffer::OwnedImpl request(encryptionRequest(Postgres::Protocol::SSL_REQUEST_CODE));
  ASSERT_EQ(filter_->onData(request, false), Network::FilterStatus::Continue);
  Buffer::OwnedImpl reply("N");
  ASSERT_EQ(filter_->onWrite(reply, false), Network::FilterStatus::Continue);
  EXPECT_EQ(reply.toString(), "N");
  EXPECT_EQ(config_->stats_.sessions_encrypted_.value(), 0);

  const bool allowed = GetParam();
  const std::string packet = allowed ? startup() : startup("user\0blocked\0\0"s);
  Buffer::OwnedImpl data(packet.substr(0, 10));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);

  if (!allowed) {
    expectDenied("rbac_access_denied_matched_policy[blocked_user]");
  }
  data.add(packet.substr(10));
  EXPECT_EQ(filter_->onData(data, false),
            allowed ? Network::FilterStatus::Continue : Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.toString(), allowed ? packet : "");
  EXPECT_EQ(packetType(), "startup_message");
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), allowed ? 2 : 1);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), allowed ? 0 : 1);
}

INSTANTIATE_TEST_SUITE_P(StartupPolicyOutcomes, PostgresRbacSSLFallbackTest, testing::Bool());

TEST_F(PostgresRbacTest, RejectsClientDataWhileAwaitingPassthroughReply) {
  rules("action: DENY\npolicies: {}\n");
  Buffer::OwnedImpl request(encryptionRequest(Postgres::Protocol::SSL_REQUEST_CODE));
  ASSERT_EQ(filter_->onData(request, false), Network::FilterStatus::Continue);
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  Buffer::OwnedImpl early(startup());
  EXPECT_EQ(filter_->onData(early, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(early.length(), 0);
}

TEST_F(PostgresRbacTest, RejectsRepeatedSSLRequestAfterServerDeclines) {
  rules("action: DENY\npolicies: {}\n");
  const std::string packet = encryptionRequest(Postgres::Protocol::SSL_REQUEST_CODE);
  Buffer::OwnedImpl request(packet);
  ASSERT_EQ(filter_->onData(request, false), Network::FilterStatus::Continue);
  Buffer::OwnedImpl reply("N");
  ASSERT_EQ(filter_->onWrite(reply, false), Network::FilterStatus::Continue);

  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  Buffer::OwnedImpl repeated(packet);
  EXPECT_EQ(filter_->onData(repeated, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(repeated.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
}

TEST_F(PostgresRbacTest, RelaysBackendErrorBeforeClientStartup) {
  rules(Rules);
  auto reply = filter_->getEncoder()->buildErrorResponse("FATAL", "cannot connect", "57P03");
  const std::string original = reply.toString();
  EXPECT_EQ(filter_->onWrite(reply, false), Network::FilterStatus::Continue);
  EXPECT_EQ(reply.toString(), original);
  EXPECT_TRUE(info_.metadata_.filter_metadata().empty());

  Buffer::OwnedImpl data(startup());
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
}

class PostgresRbacInvalidSSLReplyTest : public PostgresRbacTest,
                                        public testing::WithParamInterface<std::string> {};

TEST_P(PostgresRbacInvalidSSLReplyTest, ClosesWithoutRelayingUnexpectedReply) {
  rules("action: DENY\npolicies: {}\n");
  Buffer::OwnedImpl request(encryptionRequest(Postgres::Protocol::SSL_REQUEST_CODE));
  ASSERT_EQ(filter_->onData(request, false), Network::FilterStatus::Continue);
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  Buffer::OwnedImpl reply(GetParam());
  EXPECT_EQ(filter_->onWrite(reply, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(reply.length(), 0);
  EXPECT_EQ(config_->stats_.sessions_encrypted_.value(), 0);
}

INSTANTIATE_TEST_SUITE_P(InvalidSSLReplies, PostgresRbacInvalidSSLReplyTest,
                         testing::Values("X", "SN"));

class PostgresRbacGSSFallbackTest : public PostgresRbacTest,
                                    public testing::WithParamInterface<bool> {};

TEST_P(PostgresRbacGSSFallbackTest, DeclinesGSSAndAuthorizesTheNextPacket) {
  rules("action: DENY\npolicies: {}\n");
  const std::string request = encryptionRequest(Postgres::Protocol::GSSAPI_ENC_REQUEST_CODE);
  Buffer::OwnedImpl data(request.substr(0, 4));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_CALL(write_, injectWriteDataToFilterChain(_, false))
      .WillOnce(Invoke([](Buffer::Instance& reply, bool) { EXPECT_EQ(reply.toString(), "N"); }));
  data.add(request.substr(4));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_TRUE(info_.metadata_.filter_metadata().empty());
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);

  if (GetParam()) {
    const std::string ssl = encryptionRequest(Postgres::Protocol::SSL_REQUEST_CODE);
    data.add(ssl);
    ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
    EXPECT_EQ(data.toString(), ssl);
    data.drain(data.length());
    Buffer::OwnedImpl reply("N");
    ASSERT_EQ(filter_->onWrite(reply, false), Network::FilterStatus::Continue);
    EXPECT_EQ(reply.toString(), "N");
  }
  const std::string packet = startup();
  data.add(packet);
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), packet);
  EXPECT_EQ(packetType(), "startup_message");
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), GetParam() ? 2 : 1);
}

INSTANTIATE_TEST_SUITE_P(GSSFallbacks, PostgresRbacGSSFallbackTest, testing::Bool());

TEST_F(PostgresRbacTest, RejectsRepeatedGSSRequest) {
  rules("action: DENY\npolicies: {}\n");
  const std::string request = encryptionRequest(Postgres::Protocol::GSSAPI_ENC_REQUEST_CODE);
  Buffer::OwnedImpl data(request);
  EXPECT_CALL(write_, injectWriteDataToFilterChain(_, false))
      .WillOnce(Invoke([](Buffer::Instance& reply, bool) { EXPECT_EQ(reply.toString(), "N"); }));
  ASSERT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  data.add(request);
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
}

TEST_F(PostgresRbacTest, RejectsDataPipelinedBehindGSSRequest) {
  rules("action: DENY\npolicies: {}\n");
  Buffer::OwnedImpl data(encryptionRequest(Postgres::Protocol::GSSAPI_ENC_REQUEST_CODE) +
                         startup());
  EXPECT_CALL(connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(data.length(), 0);
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 0);
}

TEST_F(PostgresRbacTest, LogRulesDoNotRejectAnOversizedFirstPacket) {
  rules("action: LOG\npolicies: {}\n");
  Buffer::OwnedImpl data;
  data.writeBEInt<uint32_t>(10001);
  const std::string packet = data.toString();
  EXPECT_CALL(connection_, close(_)).Times(0);
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), packet);
  EXPECT_EQ(config_->stats_.authorization_denied_.value(), 0);
}

TEST_F(PostgresRbacTest, LogRulesLeaveGSSEncryptionNegotiationToTheServer) {
  rules("action: LOG\npolicies: {}\n");
  const std::string request = encryptionRequest(Postgres::Protocol::GSSAPI_ENC_REQUEST_CODE);
  Buffer::OwnedImpl data(request);
  EXPECT_EQ(filter_->onData(data, false), Network::FilterStatus::Continue);
  EXPECT_EQ(data.toString(), request);
  EXPECT_EQ(packetType(), "gss_enc_request");
  EXPECT_EQ(config_->stats_.authorization_allowed_.value(), 1);
}

TEST_F(PostgresRbacTest, EmptyStartupValueDoesNotBypassUserDenyRule) {
  rules(DenyBlockedUser);
  expectDenied("rbac_access_denied_matched_policy[blocked_user]");
  Buffer::OwnedImpl data(startup("application_name\0\0user\0blocked\0database\0testdb\0\0"s));
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::StopIteration);
  ASSERT_THAT(data.length(), 0);
  ASSERT_THAT(metadata().fields().at("user").string_value(), "blocked");
  EXPECT_TRUE(metadata().fields().at("application_name").string_value().empty());
  EXPECT_EQ(metadata().fields().count(""), 0);
  ASSERT_THAT(config_->stats_.authorization_denied_.value(), 1);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 0);
}

} // namespace
} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
