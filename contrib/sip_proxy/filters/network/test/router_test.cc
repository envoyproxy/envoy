#include <chrono>
#include <memory>

#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"

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
  void initializeTrans(bool has_option = true) {
    if (has_option == true) {
      const std::string yaml = R"EOF(
session_affinity: true
registration_affinity: true
)EOF";

      envoy::extensions::filters::network::sip_proxy::v3alpha::SipProtocolOptions config;
      TestUtility::loadFromYaml(yaml, config);

      const auto options = std::make_shared<ProtocolOptionsConfigImpl>(config);
      EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
                  extensionProtocolOptions(_))
          .WillRepeatedly(Return(options));
    }

    transaction_infos_ = std::make_shared<TransactionInfos>();
    context_.cluster_manager_.initializeThreadLocalClusters({"cluster"});
  }

  void initializeRouter() {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    router_ = std::make_unique<Router>(context_.clusterManager(), "test", context_.scope());

    EXPECT_EQ(nullptr, router_->downstreamConnection());

    EXPECT_CALL(callbacks_, transactionInfos()).WillOnce(Return(transaction_infos_));
    router_->setDecoderFilterCallbacks(callbacks_);
  }

  void initializeRouterWithCallback() {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    router_ = std::make_unique<Router>(context_.clusterManager(), "test", context_.scope());

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
    metadata_->setRouteEP("10.0.0.1");
    metadata_->setRouteOpaque("10.0.0.1");
    metadata_->setDomain(
        "<sip:10.0.0.1;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip="
        "192.169.110.50;x-skey=000075b77a8f02240001;x-fbi=cfed;ue-addr=10.30.29.58>",
        "host");
    if (set_destination) {
      metadata_->setDestination("10.0.0.1");
    }
  }

  void initializeTransaction() {
    auto transaction_info_ptr = std::make_shared<TransactionInfo>(
        "test", thread_local_, static_cast<std::chrono::seconds>(2), "", "x-suri");
    transaction_info_ptr->init();
    transaction_infos_->emplace(cluster_name_, transaction_info_ptr);
  }

  void startRequest(MsgType msg_type, MethodType method = MethodType::Invite) {
    // const bool strip_service_name = false)
    initializeMetadata(msg_type, method);
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

    EXPECT_CALL(callbacks_, route()).WillRepeatedly(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillRepeatedly(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));

    EXPECT_CALL(callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_EQ(&connection_, router_->downstreamConnection());

    EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
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

  void returnResponse(MsgType msg_type = MsgType::Response, bool is_success = true) {
    Buffer::OwnedImpl buffer;

    initializeMetadata(msg_type, MethodType::Ok200, false);

    ON_CALL(callbacks_, responseSuccess()).WillByDefault(Return(is_success));

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

  std::shared_ptr<TransactionInfos> transaction_infos_;

  RouteConstSharedPtr route_ptr_;
  std::unique_ptr<Router> router_;

  std::string cluster_name_{"cluster"};

  MsgType msg_type_{MsgType::Request};
  MessageMetadataSharedPtr metadata_;

  Tcp::ConnectionPool::UpstreamCallbacks* upstream_callbacks_{};
  NiceMock<Network::MockClientConnection> upstream_connection_;
};

TEST_F(SipRouterTest, Call) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  startRequest(MsgType::Request);
  connectUpstream();
  completeRequest();
  returnResponse();
  EXPECT_CALL(callbacks_, transactionId()).WillRepeatedly(Return("test"));
  destroyRouter();
}

TEST_F(SipRouterTest, CallWithNotify) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request, MethodType::Notify);
  metadata_->setEP("10.0.0.1");
  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));

  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  EXPECT_NE(nullptr, transaction_info_ptr);
  std::shared_ptr<UpstreamRequest> upstream_request_ptr =
      transaction_info_ptr->getUpstreamRequest("10.0.0.1");
  EXPECT_NE(nullptr, upstream_request_ptr);
  upstream_request_ptr->resetStream();

  transaction_info_ptr->deleteUpstreamRequest("10.0.0.1");
  upstream_request_ptr = transaction_info_ptr->getUpstreamRequest("10.0.0.1");
  EXPECT_EQ(nullptr, upstream_request_ptr);
}

TEST_F(SipRouterTest, DiffRouter) {
  initializeTrans(false);
  initializeRouter();
  router_->metadataMatchCriteria();
  EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
  initializeTransaction();
  EXPECT_EQ(router_->metadataMatchCriteria(), route_entry_.metadataMatchCriteria());
  startRequest(MsgType::Request);

  initializeRouter();
  startRequest(MsgType::Request);
}

TEST_F(SipRouterTest, DiffRouterDiffTrans) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  startRequest(MsgType::Request);

  initializeRouter();

  initializeMetadata(MsgType::Request, MethodType::Invite);
  EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

  metadata_->setTransactionId("cluster");
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
}

TEST_F(SipRouterTest, DiffDestination) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request, MethodType::Register);
  metadata_->setEP("10.0.0.1");
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));

  initializeRouter();
  initializeMetadata(MsgType::Request, MethodType::Register);
  metadata_->setEP("10.0.0.1");
  metadata_->setDestination("10.0.0.1");

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
}

TEST_F(SipRouterTest, DiffDestinationDiffTrans) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  startRequest(MsgType::Request);

  initializeRouter();
  initializeMetadata(MsgType::Request, MethodType::Ack);
  metadata_->setDestination("10.0.0.1");

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

  metadata_->setTransactionId("cluster");
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
}

TEST_F(SipRouterTest, DiffDestinationNoTrans) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  startRequest(MsgType::Request);

  initializeRouter();
  initializeMetadata(MsgType::Request, MethodType::Ack);
  metadata_->setDestination("10.0.0.1");

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

  metadata_->setTransactionId("<branch=testNoTrans>");
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
}

TEST_F(SipRouterTest, NoDestination) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();

  initializeMetadata(MsgType::Request, MethodType::Invite, false);
  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
}

TEST_F(SipRouterTest, CallNoRouter) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::UnknownMethod, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*no route.*"));
        EXPECT_TRUE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
  EXPECT_EQ(1U, context_.scope().counterFromString("test.route_missing").value());

  destroyRouterOutofRange();
}

TEST_F(SipRouterTest, CallNoCluster) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_, getThreadLocalCluster(Eq(cluster_name_)))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*unknown cluster.*"));
        EXPECT_TRUE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
  EXPECT_EQ(1U, context_.scope().counterFromString("test.unknown_cluster").value());

  destroyRouter();
}

TEST_F(SipRouterTest, ClusterMaintenanceMode) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(*context_.cluster_manager_.thread_local_cluster_.cluster_.info_, maintenanceMode())
      .WillOnce(Return(true));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*maintenance mode.*"));
        EXPECT_TRUE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
  EXPECT_EQ(1U, context_.scope().counterFromString("test.upstream_rq_maintenance_mode").value());
  destroyRouter();
}

TEST_F(SipRouterTest, NoHealthyHosts) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillOnce(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_, tcpConnPool(_, _))
      .WillOnce(Return(absl::nullopt));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*no healthy upstream.*"));
        EXPECT_TRUE(end_stream);
      }));
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
  EXPECT_EQ(1U, context_.scope().counterFromString("test.no_healthy_upstream").value());
  destroyRouter();
}

TEST_F(SipRouterTest, NoHost) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillOnce(ReturnRef(cluster_name_));

  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, host())
      .WillOnce(Return(nullptr));
  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
  destroyRouter();
}

TEST_F(SipRouterTest, NoNewConnection) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  initializeMetadata(MsgType::Request);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillOnce(ReturnRef(cluster_name_));

  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
      .WillOnce(Return(nullptr));

  EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
  destroyRouter();
}

TEST_F(SipRouterTest, CallWithExistingConnection) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  startRequest(MsgType::Request);
  connectUpstream();
  completeRequest();
  returnResponse();
  metadata_->setDestination("10.0.0.1");
  router_->cleanup();
  startRequestWithExistingConnection(MsgType::Request);
  destroyRouter();
}

TEST_F(SipRouterTest, PoolFailure) {
  initializeTrans();
  initializeRouterWithCallback();
  initializeTransaction();
  startRequest(MsgType::Request);
  // connectUpstream();
  context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolFailure(
      ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  completeRequest();
}

TEST_F(SipRouterTest, NewConnectionFailure) {
  initializeTrans();
  initializeRouterWithCallback();
  initializeTransaction();
  EXPECT_CALL(context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_, newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.newConnectionImpl(cb);
            context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
                upstream_connection_);
            return nullptr;
          }));
  startRequest(MsgType::Request);
}

TEST_F(SipRouterTest, UpstreamCloseMidResponse) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  startRequest(MsgType::Request);
  connectUpstream();

  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  upstream_callbacks_->onEvent(Network::ConnectionEvent::RemoteClose);
  // Panic: NOT_REACHED_GCOVR_EXCL_LINE
  // upstream_callbacks_->onEvent(static_cast<Network::ConnectionEvent>(9999));
}

TEST_F(SipRouterTest, RouteEntryImplBase) {
  const envoy::extensions::filters::network::sip_proxy::v3alpha::Route route;
  GeneralRouteEntryImpl* base = new GeneralRouteEntryImpl(route);
  EXPECT_EQ("", base->clusterName());
  EXPECT_EQ(base, base->routeEntry());
  EXPECT_EQ(nullptr, base->metadataMatchCriteria());
}

envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration
parseConfigFromYaml(const std::string& yaml) {
  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration route;
  TestUtility::loadFromYaml(yaml, route);
  return route;
}

TEST_F(SipRouterTest, RouteMatcher) {

  const std::string yaml = R"EOF(
  name: local_route
  routes:
    match:
      domain: pcsf-cfed.cncs.svc.cluster.local
    route:
      cluster: A
)EOF";

  envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration config;
  TestUtility::loadFromYaml(yaml, config);

  initializeMetadata(MsgType::Request);
  auto matcher_ptr = std::make_shared<RouteMatcher>(config);

  // Match domain
  metadata_->setDomain("<sip:10.177.8.232;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip="
                       "192.169.110.50;x-skey=000075b77a8f02240001;x-fbi=cfed;ue-addr=10.30.29.58>",
                       "x-suri");
  matcher_ptr->route(*metadata_);

  // Not match domain
  metadata_->setDomain(
      "<sip:10.177.8.232;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local.test:5060;inst-ip=192.169.110."
      "50;x-skey=000075b77a8f02240001;x-fbi=cfed;ue-addr=10.30.29.58>",
      "x-suri");
  matcher_ptr->route(*metadata_);
}

TEST_F(SipRouterTest, HandlePendingRequest) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  startRequest(MsgType::Request);
  connectUpstream();
  completeRequest();

  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  EXPECT_NE(nullptr, transaction_info_ptr);
  std::shared_ptr<UpstreamRequest> upstream_request_ptr =
      transaction_info_ptr->getUpstreamRequest("10.0.0.1");
  EXPECT_NE(nullptr, upstream_request_ptr);
  upstream_request_ptr->addIntoPendingRequest(metadata_);
  // trigger full
  upstream_request_ptr->onRequestStart();

  for (int i = 0; i < 1000003; i++) {
    upstream_request_ptr->addIntoPendingRequest(metadata_);
  }

  upstream_request_ptr->resetStream();

  // Other UpstreamRequest in definition
  upstream_request_ptr->onAboveWriteBufferHighWatermark();
  upstream_request_ptr->onBelowWriteBufferLowWatermark();
}

TEST_F(SipRouterTest, ResponseDecoder) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  startRequest(MsgType::Request);

  initializeMetadata(MsgType::Response, MethodType::Ok200);
  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  EXPECT_NE(nullptr, transaction_info_ptr);
  std::shared_ptr<UpstreamRequest> upstream_request_ptr =
      transaction_info_ptr->getUpstreamRequest("10.0.0.1");
  EXPECT_NE(nullptr, upstream_request_ptr);
  std::shared_ptr<ResponseDecoder> response_decoder_ptr =
      std::make_shared<ResponseDecoder>(*upstream_request_ptr);
  EXPECT_EQ(FilterStatus::Continue, response_decoder_ptr->transportBegin(metadata_));
  EXPECT_EQ(FilterStatus::Continue, response_decoder_ptr->messageBegin(metadata_));
  EXPECT_EQ(FilterStatus::Continue, response_decoder_ptr->messageEnd());
  EXPECT_EQ(FilterStatus::Continue, response_decoder_ptr->transportEnd());
  response_decoder_ptr->newDecoderEventHandler(metadata_);

  // No active trans
  metadata_->setTransactionId(nullptr);
  EXPECT_EQ(FilterStatus::Continue, response_decoder_ptr->transportBegin(metadata_));
  // No transid
  metadata_->resetTransactionId();
  EXPECT_EQ(FilterStatus::StopIteration, response_decoder_ptr->transportBegin(metadata_));
}

TEST_F(SipRouterTest, TransactionInfoItem) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  startRequest(MsgType::Request);

  initializeMetadata(MsgType::Request);
  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  EXPECT_NE(nullptr, transaction_info_ptr);
  std::shared_ptr<UpstreamRequest> upstream_request_ptr =
      transaction_info_ptr->getUpstreamRequest("10.0.0.1");
  EXPECT_NE(nullptr, upstream_request_ptr);

  std::shared_ptr<TransactionInfoItem> item =
      std::make_shared<TransactionInfoItem>(&callbacks_, upstream_request_ptr);
  item->appendMessageList(metadata_);
  item->resetTrans();
  EXPECT_NE(nullptr, item->upstreamRequest());
  EXPECT_EQ(false, item->deleted());
}

TEST_F(SipRouterTest, Audit) {
  initializeTrans();
  initializeRouter();
  initializeTransaction();
  startRequest(MsgType::Request);

  initializeMetadata(MsgType::Request);
  auto& transaction_info_ptr = (*transaction_infos_)[cluster_name_];
  EXPECT_NE(nullptr, transaction_info_ptr);
  std::shared_ptr<UpstreamRequest> upstream_request_ptr =
      transaction_info_ptr->getUpstreamRequest("10.0.0.1");
  EXPECT_NE(nullptr, upstream_request_ptr);

  std::shared_ptr<TransactionInfoItem> item =
      std::make_shared<TransactionInfoItem>(&callbacks_, upstream_request_ptr);
  std::shared_ptr<TransactionInfoItem> itemToDelete =
      std::make_shared<TransactionInfoItem>(&callbacks_, upstream_request_ptr);
  itemToDelete->toDelete();
  ThreadLocalTransactionInfo threadInfo(transaction_info_ptr, dispatcher_,
                                        std::chrono::milliseconds(0), "", "x-suri");
  threadInfo.transaction_info_map_.emplace(cluster_name_, item);
  threadInfo.transaction_info_map_.emplace("test1", itemToDelete);
  threadInfo.auditTimerAction();
}

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
