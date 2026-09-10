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

  void expectDenied(absl::string_view details = "rbac_access_denied_matched_policy[]") {
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
  const std::string packet = startup();
  Buffer::OwnedImpl data(packet);
  ASSERT_THAT(filter_->onData(data, false), Network::FilterStatus::Continue);
  ASSERT_THAT(config_->stats_.authorization_allowed_.value(), 1);
  ASSERT_THAT(metadata().fields().at("user").string_value(), "postgres");
  ASSERT_THAT(metadata().fields().at("database").string_value(), "testdb");
}

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
