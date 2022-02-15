#include <chrono>
#include <cstddef>
#include <memory>

#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_time.h"

#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"
#include "contrib/sip_proxy/filters/network/source/config.h"
#include "contrib/sip_proxy/filters/network/source/router/config.h"
#include "contrib/sip_proxy/filters/network/source/router/router_impl.h"
#include "contrib/sip_proxy/filters/network/source/sip.h"
#include "contrib/sip_proxy/filters/network/test/mocks.h"
#include "contrib/sip_proxy/filters/network/test/utility.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ContainsRegex;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

class SipRouterTest : public testing::Test {
public:
  SipRouterTest() = default;
  void initializeTrans() {
    const std::string sip_proxy_yaml = R"EOF(
           stat_prefix: egress_sip
           route_config:
             routes:
             - match:
                domain: "icscf-internal.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster
             - match:
                domain: "scscf-internal.cncs.svc.cluster.local"
                header: "Route"
                parameter: "x-suri"
               route:
                cluster: fake_cluster2
           settings:
             transaction_timeout: 32s
             local_services:
             - domain: "pcsf-cfed.cncs.svc.cluster.local"
               parameter: "x-suri"
             tra_service_config:
               grpc_service:
                 envoy_grpc:
                   cluster_name: tra_service
               timeout: 2s
               transport_api_version: V3
)EOF";

    const std::string sip_protocol_options_yaml = R"EOF(
        session_affinity: true
        registration_affinity: true
        customized_affinity:
          entries:
          - key_name: lskpmc
            query: true
            subscribe: true
          - key_name: ep
            query: false
            subscribe: false
)EOF";

    TestUtility::loadFromYaml(sip_protocol_options_yaml, sip_protocol_options_config_);
    TestUtility::loadFromYaml(sip_proxy_yaml, sip_proxy_config_);

    const auto options = std::make_shared<ProtocolOptionsConfigImpl>(sip_protocol_options_config_);
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
                extensionProtocolOptions(_))
        .WillRepeatedly(Return(options));

    transaction_infos_ = std::make_shared<TransactionInfos>();
    context_.cluster_manager_.initializeThreadLocalClusters({cluster_name_});

    // TODO
    // DangerousDeprecatedTestTime test_time_;
    StreamInfo::StreamInfoImpl stream_info{time_source_, nullptr};
    SipFilterStats stat = SipFilterStats::generateStats("test.", store_);
    EXPECT_CALL(config_, stats()).WillRepeatedly(ReturnRef(stat));
    filter_ =
        new NiceMock<MockConnectionManager>(config_, random_, time_source_, context_, nullptr);
    tra_handler_ = std::make_shared<NiceMock<SipProxy::MockTrafficRoutingAssistantHandler>>(
        *filter_, sip_proxy_config_.settings().tra_service_config(), context_, stream_info);
  }

  void initializeRouter() {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    router_ =
        std::make_unique<Router>(context_.clusterManager(), "test", context_.scope(), context_);

    EXPECT_EQ(nullptr, router_->downstreamConnection());

    EXPECT_CALL(callbacks_, transactionInfos()).WillOnce(Return(transaction_infos_));
    EXPECT_CALL(callbacks_, traHandler()).WillRepeatedly(Return(tra_handler_));
    router_->setDecoderFilterCallbacks(callbacks_);
  }

  void initializeRouterWithCallback() {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    router_ =
        std::make_unique<Router>(context_.clusterManager(), "test", context_.scope(), context_);

    EXPECT_CALL(callbacks_, transactionInfos()).WillOnce(Return(transaction_infos_));
    router_->setDecoderFilterCallbacks(callbacks_);

    EXPECT_EQ(nullptr, router_->downstreamConnection());
  }

  void initializeMetadata(MsgType msg_type, MethodType method = MethodType::Invite,
                          bool set_destination = true) {

    metadata_ = std::make_shared<MessageMetadata>();
    metadata_->setMethodType(method);
    metadata_->setMsgType(msg_type);
    metadata_->setTransactionId("<branch=cluster>");
    metadata_->setEP("10.0.0.1");
    // metadata_->setDomain(
    //   "<sip:10.0.0.1;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip="
    //  "192.169.110.50;x-skey=000075b77a8f02240001;x-fbi=cfed;ue-addr=10.30.29.58>",
    // "host");
    if (set_destination) {
      metadata_->setDestination("10.0.0.1");
    }
  }

  void initializeTransaction() {
    // TODO
    auto transaction_info_ptr = std::make_shared<TransactionInfo>(
        cluster_name_, thread_local_, static_cast<std::chrono::seconds>(2),
        sip_proxy_config_.settings().local_services());
    transaction_info_ptr->init();
    transaction_infos_->emplace(cluster_name_, transaction_info_ptr);
  }

  void startRequest(MsgType msg_type, MethodType method = MethodType::Invite) {
    // const bool strip_service_name = false)
    initializeMetadata(msg_type, method);
    EXPECT_CALL(callbacks_, route()).WillRepeatedly(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

    // EXPECT_CALL(callbacks_, route()).WillRepeatedly(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillRepeatedly(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
  }

  void connectUpstream() {
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                addUpstreamCallbacks(_))
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;
        }));

    conn_state_.reset();
    EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.connection_data_,
                connectionState())
        .WillRepeatedly(
            Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));
    context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(upstream_connection_);

    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void startRequestWithExistingConnection(MsgType msg_type,
                                          MethodType method = MethodType::Invite) {
    initializeMetadata(msg_type, method);
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin({}));

    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void completeRequest() {
    EXPECT_EQ(FilterStatus::Continue, router_->messageEnd());
    EXPECT_EQ(FilterStatus::Continue, router_->transportEnd());
  }

  void returnResponse(MsgType msg_type = MsgType::Response) {
    Buffer::OwnedImpl buffer;

    initializeMetadata(msg_type, MethodType::Ok200, false);

    // ON_CALL(callbacks_, responseSuccess()).WillByDefault(Return(is_success));

    upstream_callbacks_->onUpstreamData(buffer, false);
  }

  void destroyRouter() {
    router_->onDestroy();
    router_.reset();
  }
  void destroyRouterOutofRange() {
    // std::out_of_range Exception
    EXPECT_CALL(callbacks_, transactionId())
        .Times(2)
        .WillOnce(Return("test"))
        .WillOnce(Return("test1"));

    router_->onDestroy();
    router_.reset();
  }

  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProtocolOptions
      sip_protocol_options_config_;
  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy sip_proxy_config_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Network::MockClientConnection> connection_;
  NiceMock<StreamInfo::MockStreamInfo> streamInfo_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<MockTimeSystem> time_source_;
  NiceMock<SipFilters::MockDecoderFilterCallbacks> callbacks_;
  NiceMock<MockRoute>* route_{};
  NiceMock<MockRouteEntry> route_entry_;
  NiceMock<Upstream::MockHostDescription>* host_{};
  Tcp::ConnectionPool::ConnectionStatePtr conn_state_;
  Buffer::OwnedImpl buffer_;
  NiceMock<ThreadLocal::MockInstance> thread_local_;
  NiceMock<MockConnectionManager>* filter_{};
  NiceMock<MockConfig> config_;
  NiceMock<Random::MockRandomGenerator> random_;
  Stats::TestUtil::TestStore store_;

  std::shared_ptr<TransactionInfos> transaction_infos_;

  RouteConstSharedPtr route_ptr_;
  std::unique_ptr<Router> router_;

  std::shared_ptr<SipProxy::MockTrafficRoutingAssistantHandler> tra_handler_;

  std::string cluster_name_{"fake_cluster"};

  MsgType msg_type_{MsgType::Request};
  MessageMetadataSharedPtr metadata_;

  Tcp::ConnectionPool::UpstreamCallbacks* upstream_callbacks_{};
  NiceMock<Network::MockClientConnection> upstream_connection_;
};

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
