#include <memory>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/filter/thrift/router/v2alpha1/router.pb.h"
#include "envoy/config/filter/thrift/router/v2alpha1/router.pb.validate.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "source/extensions/filters/network/thrift_proxy/config.h"
#include "source/extensions/filters/network/thrift_proxy/router/config.h"
#include "source/extensions/filters/network/thrift_proxy/router/router_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/shadow_writer_impl.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/printers.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AtLeast;
using testing::Bool;
using testing::Combine;
using testing::ContainsRegex;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using ::testing::TestParamInfo;
using testing::Values;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {
namespace {

class TestNamedTransportConfigFactory : public NamedTransportConfigFactory {
public:
  TestNamedTransportConfigFactory(std::function<MockTransport*()> f) : f_(f) {}

  TransportPtr createTransport() override { return TransportPtr{f_()}; }
  std::string name() const override { return TransportNames::get().FRAMED; }

  std::function<MockTransport*()> f_;
};

class TestNamedProtocolConfigFactory : public NamedProtocolConfigFactory {
public:
  TestNamedProtocolConfigFactory(std::function<MockProtocol*()> f) : f_(f) {}

  ProtocolPtr createProtocol() override { return ProtocolPtr{f_()}; }
  std::string name() const override { return ProtocolNames::get().BINARY; }

  std::function<MockProtocol*()> f_;
};

} // namespace

class ThriftRouterTestBase {
public:
  ThriftRouterTestBase()
      : transport_factory_([&]() -> MockTransport* {
          // Create shadow transports.
          auto transport = new NiceMock<MockTransport>();
          transports_requested_++;

          // Ignore null response decoder transports.
          bool is_response_transport = shadow_writer_impl_ != nullptr &&
                                       (transports_requested_ == 1 || transports_requested_ == 3);
          if (!is_response_transport) {
            if (mock_transport_cb_) {
              mock_transport_cb_(transport);
            }
            all_transports_.push_back(transport);
            transport_ = transport;
          }

          return transport;
        }),
        protocol_factory_([&]() -> MockProtocol* {
          // Create shadow protocols.
          auto protocol = new NiceMock<MockProtocol>();
          protocols_requested_++;

          // Ditto for protocols.
          bool is_response_protocol = shadow_writer_impl_ != nullptr &&
                                      (protocols_requested_ == 1 || protocols_requested_ == 3);
          if (!is_response_protocol) {
            if (mock_protocol_cb_) {
              mock_protocol_cb_(protocol);
            }
            all_protocols_.push_back(protocol);
            protocol_ = protocol;
          }

          return protocol;
        }),
        transport_register_(transport_factory_), protocol_register_(protocol_factory_) {
    context_.server_factory_context_.cluster_manager_.initializeThreadLocalClusters({"cluster"});
  }

  void initializeRouter(ShadowWriter& shadow_writer, bool close_downstream_on_error) {
    route_ = new NiceMock<MockRoute>();
    route_ptr_.reset(route_);

    router_ = std::make_unique<Router>(context_.server_factory_context_.cluster_manager_, *stats_,
                                       context_.server_factory_context_.runtime_loader_,
                                       shadow_writer, close_downstream_on_error);

    EXPECT_EQ(nullptr, router_->downstreamConnection());
    router_->onAboveWriteBufferHighWatermark();
    router_->onBelowWriteBufferLowWatermark();

    router_->setDecoderFilterCallbacks(callbacks_);
  }

  void initializeRouter(bool close_downstream_on_error = true) {
    stats_ = std::make_shared<const RouterStats>("test", context_.scope(),
                                                 context_.server_factory_context_.localInfo());
    initializeRouter(shadow_writer_, close_downstream_on_error);
  }

  void initializeRouterWithShadowWriter() {
    stats_ = std::make_shared<const RouterStats>("test", context_.scope(),
                                                 context_.server_factory_context_.localInfo());
    shadow_writer_impl_ = std::make_shared<ShadowWriterImpl>(
        context_.server_factory_context_.clusterManager(), *stats_, dispatcher_,
        context_.server_factory_context_.threadLocal());
    initializeRouter(*shadow_writer_impl_, true);
  }

  void initializeMetadata(MessageType msg_type, std::string method = "method",
                          int32_t sequence_id = 1) {
    msg_type_ = msg_type;

    metadata_ = std::make_shared<MessageMetadata>();
    metadata_->setMethodName(method);
    metadata_->setMessageType(msg_type_);
    metadata_->setSequenceId(sequence_id);
  }

  void verifyMetadataMatchCriteriaFromRequest(bool route_entry_has_match) {
    ProtobufWkt::Struct request_struct;
    ProtobufWkt::Value val;

    // Populate metadata like StreamInfo.setDynamicMetadata() would.
    auto& fields_map = *request_struct.mutable_fields();
    val.set_string_value("v3.1");
    fields_map["version"] = val;
    val.set_string_value("devel");
    fields_map["stage"] = val;
    val.set_string_value("1");
    fields_map["xkey_in_request"] = val;
    (*callbacks_.stream_info_.metadata_
          .mutable_filter_metadata())[Envoy::Config::MetadataFilters::get().ENVOY_LB] =
        request_struct;

    // Populate route entry's metadata which will be overridden.
    val.set_string_value("v3.0");
    fields_map = *request_struct.mutable_fields();
    fields_map["version"] = val;
    fields_map.erase("xkey_in_request");
    Envoy::Router::MetadataMatchCriteriaImpl route_entry_matches(request_struct);

    if (route_entry_has_match) {
      ON_CALL(route_entry_, metadataMatchCriteria()).WillByDefault(Return(&route_entry_matches));
    } else {
      ON_CALL(route_entry_, metadataMatchCriteria()).WillByDefault(Return(nullptr));
    }

    auto match = router_->metadataMatchCriteria()->metadataMatchCriteria();
    EXPECT_EQ(match.size(), 3);
    auto it = match.begin();

    // Note: metadataMatchCriteria() keeps its entries sorted, so the order for checks
    // below matters.

    // `stage` was only set by the request, not by the route entry.
    EXPECT_EQ((*it)->name(), "stage");
    EXPECT_EQ((*it)->value().value().string_value(), "devel");
    it++;

    // `version` should be what came from the request and override the route entry's.
    EXPECT_EQ((*it)->name(), "version");
    EXPECT_EQ((*it)->value().value().string_value(), "v3.1");
    it++;

    // `xkey_in_request` was only set by the request
    EXPECT_EQ((*it)->name(), "xkey_in_request");
    EXPECT_EQ((*it)->value().value().string_value(), "1");
  }

  void verifyMetadataMatchCriteriaFromRoute(bool route_entry_has_match) {
    ProtobufWkt::Struct route_struct;
    ProtobufWkt::Value val;

    auto& fields_map = *route_struct.mutable_fields();
    val.set_string_value("v3.1");
    fields_map["version"] = val;
    val.set_string_value("devel");
    fields_map["stage"] = val;

    Envoy::Router::MetadataMatchCriteriaImpl route_entry_matches(route_struct);

    if (route_entry_has_match) {
      ON_CALL(route_entry_, metadataMatchCriteria()).WillByDefault(Return(&route_entry_matches));

      EXPECT_NE(nullptr, router_->metadataMatchCriteria());
      auto match = router_->metadataMatchCriteria()->metadataMatchCriteria();
      EXPECT_EQ(match.size(), 2);
      auto it = match.begin();

      // Note: metadataMatchCriteria() keeps its entries sorted, so the order for checks
      // below matters.

      // `stage` was set by the route entry.
      EXPECT_EQ((*it)->name(), "stage");
      EXPECT_EQ((*it)->value().value().string_value(), "devel");
      it++;

      // `version` was set by the route entry.
      EXPECT_EQ((*it)->name(), "version");
      EXPECT_EQ((*it)->value().value().string_value(), "v3.1");
    } else {
      ON_CALL(callbacks_.route_->route_entry_, metadataMatchCriteria())
          .WillByDefault(Return(nullptr));

      EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
    }
  }

  void initializeUpstreamZone() {
    upstream_locality_.set_zone("other_zone_name");
    ON_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                 .host_,
            locality())
        .WillByDefault(ReturnRef(upstream_locality_));
  }

  void startRequest(MessageType msg_type, std::string method = "method",
                    const bool strip_service_name = false,
                    const TransportType transport_type = TransportType::Framed,
                    const ProtocolType protocol_type = ProtocolType::Binary) {
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin(metadata_));

    EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    if (strip_service_name) {
      EXPECT_CALL(route_entry_, stripServiceName()).WillOnce(Return(true));
    }

    initializeMetadata(msg_type, method);

    EXPECT_CALL(callbacks_, downstreamTransportType())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(transport_type));
    EXPECT_CALL(callbacks_, downstreamProtocolType())
        .Times(AtLeast(1))
        .WillRepeatedly(Return(protocol_type));
    EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
    EXPECT_CALL(callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_EQ(&connection_, router_->downstreamConnection());

    // Not yet implemented:
    EXPECT_EQ(absl::optional<uint64_t>(), router_->computeHashKey());
    EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
    EXPECT_EQ(nullptr, router_->downstreamHeaders());
  }

  void connectUpstream() {
    EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.connection_data_,
                addUpstreamCallbacks(_))
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;
        }));

    conn_state_.reset();
    EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.connection_data_,
                connectionState())
        .WillRepeatedly(
            Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));
    EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.connection_data_,
                setConnectionState_(_))
        .WillOnce(Invoke(
            [&](Tcp::ConnectionPool::ConnectionStatePtr& cs) -> void { conn_state_.swap(cs); }));

    EXPECT_CALL(*protocol_, writeMessageBegin(_, _))
        .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
          EXPECT_EQ(metadata_->methodName(), metadata.methodName());
          EXPECT_EQ(metadata_->messageType(), metadata.messageType());
          EXPECT_EQ(metadata_->sequenceId(), metadata.sequenceId());
        }));

    EXPECT_CALL(callbacks_, continueDecoding());
    context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
        .poolReady(upstream_connection_);

    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void startRequestWithExistingConnection(MessageType msg_type, int32_t sequence_id = 1) {
    EXPECT_EQ(FilterStatus::Continue, router_->transportBegin({}));

    EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
    EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
    EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));

    initializeMetadata(msg_type, "method", sequence_id);

    EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.connection_data_,
                addUpstreamCallbacks(_))
        .WillOnce(Invoke([&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void {
          upstream_callbacks_ = &cb;
        }));

    if (!conn_state_) {
      conn_state_ = std::make_unique<ThriftConnectionState>();
    }
    EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.connection_data_,
                connectionState())
        .WillRepeatedly(
            Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));

    EXPECT_CALL(callbacks_, connection()).WillRepeatedly(Return(&connection_));
    EXPECT_CALL(callbacks_, dispatcher()).WillRepeatedly(ReturnRef(dispatcher_));
    EXPECT_EQ(&connection_, router_->downstreamConnection());

    // Not yet implemented:
    EXPECT_EQ(absl::optional<uint64_t>(), router_->computeHashKey());
    EXPECT_EQ(nullptr, router_->metadataMatchCriteria());
    EXPECT_EQ(nullptr, router_->downstreamHeaders());

    EXPECT_CALL(callbacks_, downstreamTransportType())
        .Times(1)
        .WillRepeatedly(Return(TransportType::Framed));
    EXPECT_CALL(callbacks_, downstreamProtocolType())
        .Times(1)
        .WillRepeatedly(Return(ProtocolType::Binary));

    mock_protocol_cb_ = [&](MockProtocol* protocol) -> void {
      ON_CALL(*protocol, type()).WillByDefault(Return(ProtocolType::Binary));
      EXPECT_CALL(*protocol, writeMessageBegin(_, _))
          .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
            EXPECT_EQ(metadata_->methodName(), metadata.methodName());
            EXPECT_EQ(metadata_->messageType(), metadata.messageType());
            EXPECT_EQ(metadata_->sequenceId(), metadata.sequenceId());
          }));
    };
    EXPECT_CALL(callbacks_, continueDecoding()).Times(0);
    EXPECT_CALL(
        context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
        newConnection(_))
        .WillOnce(
            Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
              context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .newConnectionImpl(cb);
              context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .poolReady(upstream_connection_);
              return nullptr;
            }));

    EXPECT_EQ(FilterStatus::Continue, router_->messageBegin(metadata_));
    EXPECT_NE(nullptr, upstream_callbacks_);
  }

  void sendTrivialStruct(FieldType field_type) {
    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeStructBegin(_, ""));
    }
    EXPECT_EQ(FilterStatus::Continue, router_->structBegin({}));

    int16_t id = 1;
    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeFieldBegin(_, "", field_type, id));
    }
    EXPECT_EQ(FilterStatus::Continue, router_->fieldBegin({}, field_type, id));

    sendTrivialValue(field_type);

    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeFieldEnd(_));
    }
    EXPECT_EQ(FilterStatus::Continue, router_->fieldEnd());

    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeFieldBegin(_, "", FieldType::Stop, 0));
      EXPECT_CALL(*protocol, writeStructEnd(_));
    }
    EXPECT_EQ(FilterStatus::Continue, router_->structEnd());
  }

  void sendTrivialValue(FieldType field_type) {
    switch (field_type) {
    case FieldType::Bool: {
      bool v = true;
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeBool(_, v));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->boolValue(v));
    } break;
    case FieldType::Byte: {
      uint8_t v = 2;
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeByte(_, v));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->byteValue(v));
    } break;
    case FieldType::I16: {
      int16_t v = 3;
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeInt16(_, v));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->int16Value(v));
    } break;
    case FieldType::I32: {
      int32_t v = 4;
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeInt32(_, v));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(v));
    } break;
    case FieldType::I64: {
      int64_t v = 5;
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeInt64(_, v));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->int64Value(v));
    } break;
    case FieldType::Double: {
      double v = 6.0;
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeDouble(_, v));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->doubleValue(v));
    } break;
    case FieldType::String: {
      std::string v = "seven";
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeString(_, v));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->stringValue(v));
    } break;
    default:
      PANIC("reached unexpected code");
    }
  }

  void sendTrivialMap() {
    FieldType container_type = FieldType::I32;
    uint32_t size = 2;

    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeMapBegin(_, container_type, container_type, size));
    }
    EXPECT_EQ(FilterStatus::Continue, router_->mapBegin(container_type, container_type, size));

    for (int i = 0; i < 2; i++) {
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeInt32(_, i));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(i));

      int j = i + 100;
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeInt32(_, j));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(j));
    }

    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeMapEnd(_));
    }
    EXPECT_EQ(FilterStatus::Continue, router_->mapEnd());
  }

  void sendTrivialList() {
    FieldType container_type = FieldType::I32;
    uint32_t size = 3;

    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeListBegin(_, container_type, size));
    }
    EXPECT_EQ(FilterStatus::Continue, router_->listBegin(container_type, size));

    for (int i = 0; i < 3; i++) {
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeInt32(_, i));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(i));
    }

    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeListEnd(_));
    }
    EXPECT_EQ(FilterStatus::Continue, router_->listEnd());
  }

  void sendTrivialSet() {
    FieldType container_type = FieldType::I32;
    uint32_t size = 4;

    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeSetBegin(_, container_type, size));
    }
    EXPECT_EQ(FilterStatus::Continue, router_->setBegin(container_type, size));

    for (int i = 0; i < 4; i++) {
      for (auto& protocol : all_protocols_) {
        EXPECT_CALL(*protocol, writeInt32(_, i));
      }
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(i));
    }

    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeSetEnd(_));
    }
    EXPECT_EQ(FilterStatus::Continue, router_->setEnd());
  }

  void sendPassthroughData() {
    Buffer::OwnedImpl buffer;
    buffer.add("hello");

    EXPECT_EQ(FilterStatus::Continue, router_->passthroughData(buffer));
  }

  void completeRequest() {
    for (auto& protocol : all_protocols_) {
      EXPECT_CALL(*protocol, writeMessageEnd(_));
    }

    for (auto& transport : all_transports_) {
      EXPECT_CALL(*transport, encodeFrame(_, _, _));
    }

    EXPECT_CALL(upstream_connection_, write(_, false));

    if (msg_type_ == MessageType::Oneway) {
      EXPECT_CALL(
          context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
          released(Ref(upstream_connection_)));
    }

    EXPECT_EQ(FilterStatus::Continue, router_->messageEnd());
    EXPECT_EQ(FilterStatus::Continue, router_->transportEnd());
  }

  void returnResponse(MessageType msg_type = MessageType::Reply, bool is_success = true,
                      bool is_drain = false, bool is_partial = false) {
    Buffer::OwnedImpl buffer;

    EXPECT_CALL(callbacks_, startUpstreamResponse(_, _));

    auto metadata = std::make_shared<MessageMetadata>();
    metadata->setMessageType(msg_type);
    metadata->setSequenceId(1);
    metadata->setDraining(is_drain);

    ON_CALL(callbacks_, responseMetadata()).WillByDefault(Return(metadata));
    ON_CALL(callbacks_, responseSuccess()).WillByDefault(Return(is_success));

    EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
        .WillOnce(Return(ThriftFilters::ResponseStatus::MoreData));
    upstream_callbacks_->onUpstreamData(buffer, false);

    if (is_partial) {
      return;
    }

    EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
        .WillOnce(Return(ThriftFilters::ResponseStatus::Complete));
    EXPECT_CALL(
        context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
        released(Ref(upstream_connection_)));

    if (is_drain) {
      EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush))
          .WillOnce(Invoke([&](Network::ConnectionCloseType) -> void {
            // Simulate the upstream connection being closed.
            upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
          }));
    }

    upstream_callbacks_->onUpstreamData(buffer, false);
  }

  void destroyRouter() {
    router_->onDestroy();
    router_.reset();
  }

  void expectStatCalls(Stats::MockStore& cluster_store) {
    Stats::MockScope& cluster_scope = cluster_store.mockScope();
    ON_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
            statsScope())
        .WillByDefault(ReturnRef(cluster_scope));

    EXPECT_CALL(cluster_store, counter("thrift.upstream_rq_call")).Times(AtLeast(1));
    EXPECT_CALL(cluster_store, counter("thrift.upstream_resp_reply")).Times(AtLeast(1));
    EXPECT_CALL(cluster_store, counter("thrift.upstream_resp_success")).Times(AtLeast(1));

    EXPECT_CALL(cluster_store,
                histogram("thrift.upstream_rq_time", Stats::Histogram::Unit::Milliseconds));
    EXPECT_CALL(cluster_store,
                deliverHistogramToSinks(
                    testing::Property(&Stats::Metric::name, "thrift.upstream_rq_time"), _));

    EXPECT_CALL(cluster_store, histogram("thrift.upstream_rq_size", Stats::Histogram::Unit::Bytes));
    EXPECT_CALL(cluster_store,
                deliverHistogramToSinks(
                    testing::Property(&Stats::Metric::name, "thrift.upstream_rq_size"), _));
    EXPECT_CALL(cluster_store,
                histogram("thrift.upstream_resp_size", Stats::Histogram::Unit::Bytes));
    EXPECT_CALL(cluster_store,
                deliverHistogramToSinks(
                    testing::Property(&Stats::Metric::name, "thrift.upstream_resp_size"), _));
  }

  TestNamedTransportConfigFactory transport_factory_;
  TestNamedProtocolConfigFactory protocol_factory_;
  Registry::InjectFactory<NamedTransportConfigFactory> transport_register_;
  Registry::InjectFactory<NamedProtocolConfigFactory> protocol_register_;

  std::function<void(MockTransport*)> mock_transport_cb_{};
  std::function<void(MockProtocol*)> mock_protocol_cb_{};

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;

  std::unique_ptr<Router> router_;
  std::shared_ptr<const RouterStats> stats_;
  MockShadowWriter shadow_writer_;
  std::shared_ptr<ShadowWriterImpl> shadow_writer_impl_;

  NiceMock<Network::MockClientConnection> connection_;
  NiceMock<MockTimeSystem> time_source_;
  NiceMock<ThriftFilters::MockDecoderFilterCallbacks> callbacks_;
  NiceMock<MockTransport>* transport_{};
  NiceMock<MockProtocol>* protocol_{};
  std::vector<NiceMock<MockTransport>*> all_transports_{};
  std::vector<NiceMock<MockProtocol>*> all_protocols_{};
  int32_t transports_requested_{};
  int32_t protocols_requested_{};
  NiceMock<MockRoute>* route_{};
  NiceMock<MockRouteEntry> route_entry_;
  envoy::config::core::v3::Locality upstream_locality_;
  Tcp::ConnectionPool::ConnectionStatePtr conn_state_;

  RouteConstSharedPtr route_ptr_;

  std::string cluster_name_{"cluster"};

  MessageType msg_type_{MessageType::Call};
  MessageMetadataSharedPtr metadata_;

  Tcp::ConnectionPool::UpstreamCallbacks* upstream_callbacks_{};
  NiceMock<Network::MockClientConnection> upstream_connection_;
};

class ThriftRouterTest : public testing::Test, public ThriftRouterTestBase {
public:
};

class ThriftRouterFieldTypeTest : public testing::TestWithParam<FieldType>,
                                  public ThriftRouterTestBase {
public:
};

INSTANTIATE_TEST_SUITE_P(PrimitiveFieldTypes, ThriftRouterFieldTypeTest,
                         Values(FieldType::Bool, FieldType::Byte, FieldType::I16, FieldType::I32,
                                FieldType::I64, FieldType::Double, FieldType::String),
                         fieldTypeParamToString);

class ThriftRouterContainerTest : public testing::TestWithParam<FieldType>,
                                  public ThriftRouterTestBase {
public:
};

INSTANTIATE_TEST_SUITE_P(ContainerFieldTypes, ThriftRouterContainerTest,
                         Values(FieldType::Map, FieldType::List, FieldType::Set),
                         fieldTypeParamToString);

class ThriftRouterPassthroughTest
    : public testing::TestWithParam<
          std::tuple<TransportType, ProtocolType, TransportType, ProtocolType>>,
      public ThriftRouterTestBase {
public:
};

static std::string downstreamUpstreamTypesToString(
    const TestParamInfo<std::tuple<TransportType, ProtocolType, TransportType, ProtocolType>>&
        params) {
  TransportType downstream_transport_type;
  ProtocolType downstream_protocol_type;
  TransportType upstream_transport_type;
  ProtocolType upstream_protocol_type;

  std::tie(downstream_transport_type, downstream_protocol_type, upstream_transport_type,
           upstream_protocol_type) = params.param;

  return fmt::format("{}{}{}{}", TransportNames::get().fromType(downstream_transport_type),
                     ProtocolNames::get().fromType(downstream_protocol_type),
                     TransportNames::get().fromType(upstream_transport_type),
                     ProtocolNames::get().fromType(upstream_protocol_type));
}

INSTANTIATE_TEST_SUITE_P(DownstreamUpstreamTypes, ThriftRouterPassthroughTest,
                         Combine(Values(TransportType::Framed, TransportType::Unframed),
                                 Values(ProtocolType::Binary, ProtocolType::Twitter),
                                 Values(TransportType::Framed, TransportType::Unframed),
                                 Values(ProtocolType::Binary, ProtocolType::Twitter)),
                         downstreamUpstreamTypesToString);

class ThriftRouterRainidayTest : public testing::TestWithParam<bool>,
                                 public ThriftRouterTestBase {};

INSTANTIATE_TEST_SUITE_P(CloseDownstreamOnError, ThriftRouterRainidayTest, Bool());

TEST_P(ThriftRouterRainidayTest, PoolRemoteConnectionFailure) {
  initializeRouter(GetParam());

  startRequest(MessageType::Call);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(),
                    ContainsRegex(
                        ".*connection failure before response start: remote connection failure.*"));
        EXPECT_EQ(GetParam(), end_stream);
      }));
  EXPECT_CALL(callbacks_, continueDecoding()).Times(GetParam() ? 0 : 1);
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception_local.remote_connection_failure")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception_local")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception")
                .value());
  EXPECT_EQ(0UL, context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.host_->stats_.rq_error_.value());
}

TEST_P(ThriftRouterRainidayTest, PoolLocalConnectionFailure) {
  initializeRouter(GetParam());

  startRequest(MessageType::Call);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(ConnectionPool::PoolFailureReason::LocalConnectionFailure);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception_local.local_connection_failure")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception_local")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception")
                .value());
  EXPECT_EQ(0UL, context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.host_->stats_.rq_error_.value());
}

TEST_P(ThriftRouterRainidayTest, PoolTimeout) {
  initializeRouter(GetParam());

  startRequest(MessageType::Call);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(),
                    ContainsRegex(".*connection failure before response start: timeout.*"));
        EXPECT_EQ(GetParam(), end_stream);
      }));
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginTimeout, _));
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(ConnectionPool::PoolFailureReason::Timeout);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception_local.timeout")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception_local")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception")
                .value());
  EXPECT_EQ(0UL, context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.host_->stats_.rq_error_.value());
}

TEST_P(ThriftRouterRainidayTest, PoolOverflowFailure) {
  initializeRouter(GetParam());

  startRequest(MessageType::Call);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*too many connections.*"));
        EXPECT_FALSE(end_stream);
      }));
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(ConnectionPool::PoolFailureReason::Overflow, true);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception_local.overflow")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception_local")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception")
                .value());
  EXPECT_EQ(0UL, context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.host_->stats_.rq_error_.value());
}

TEST_P(ThriftRouterRainidayTest, PoolConnectionFailureWithOnewayMessage) {
  initializeRouter(GetParam());
  startRequest(MessageType::Oneway);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_oneway")
                .value());

  EXPECT_CALL(callbacks_, sendLocalReply(_, Eq(GetParam())));
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception")
                .value());
  EXPECT_EQ(0UL, context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.host_->stats_.rq_error_.value());

  destroyRouter();
}

TEST_P(ThriftRouterRainidayTest, NoRoute) {
  initializeRouter(GetParam());
  initializeMetadata(MessageType::Call);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::UnknownMethod, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*no route.*"));
        EXPECT_EQ(GetParam(), end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
  EXPECT_EQ(1U, context_.scope().counterFromString("test.route_missing").value());
}

TEST_P(ThriftRouterRainidayTest, NoCluster) {
  initializeRouter(GetParam());
  initializeMetadata(MessageType::Call);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_,
              getThreadLocalCluster(Eq(cluster_name_)))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*unknown cluster.*"));
        EXPECT_EQ(GetParam(), end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
  EXPECT_EQ(1U, context_.scope().counterFromString("test.unknown_cluster").value());
}

// Test the case where both dynamic metadata match criteria
// and route metadata match criteria is not empty.
TEST_F(ThriftRouterTest, MetadataMatchCriteriaFromRequest) {
  initializeRouter();
  initializeMetadata(MessageType::Call);

  verifyMetadataMatchCriteriaFromRequest(true);
}

// Test the case where route metadata match criteria is empty
// but with non-empty dynamic metadata match criteria.
TEST_F(ThriftRouterTest, MetadataMatchCriteriaFromRequestNoRouteEntryMatch) {
  initializeRouter();
  initializeMetadata(MessageType::Call);

  verifyMetadataMatchCriteriaFromRequest(false);
}

// Test the case where dynamic metadata match criteria is empty
// but with non-empty route metadata match criteria.
TEST_F(ThriftRouterTest, MetadataMatchCriteriaFromRoute) {
  initializeRouter();
  startRequest(MessageType::Call);

  verifyMetadataMatchCriteriaFromRoute(true);
}

// Test the case where both dynamic metadata match criteria
// and route metadata match criteria is empty.
TEST_F(ThriftRouterTest, MetadataMatchCriteriaFromRouteNoRouteEntryMatch) {
  initializeRouter();
  startRequest(MessageType::Call);

  verifyMetadataMatchCriteriaFromRoute(false);
}

TEST_P(ThriftRouterRainidayTest, ClusterMaintenanceMode) {
  initializeRouter(GetParam());
  initializeMetadata(MessageType::Call);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(
      *context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
      maintenanceMode())
      .WillOnce(Return(true));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*maintenance mode.*"));
        EXPECT_EQ(GetParam(), end_stream);
      }));
  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
  EXPECT_EQ(1U, context_.scope().counterFromString("test.upstream_rq_maintenance_mode").value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
}

TEST_P(ThriftRouterRainidayTest, NoHealthyHosts) {
  initializeRouter(GetParam());
  initializeMetadata(MessageType::Call);

  EXPECT_CALL(callbacks_, route()).WillOnce(Return(route_ptr_));
  EXPECT_CALL(*route_, routeEntry()).WillOnce(Return(&route_entry_));
  EXPECT_CALL(route_entry_, clusterName()).WillRepeatedly(ReturnRef(cluster_name_));
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_,
              tcpConnPool(_, _))
      .WillOnce(Return(absl::nullopt));

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*no healthy upstream.*"));
        EXPECT_EQ(GetParam(), end_stream);
      }));

  EXPECT_EQ(FilterStatus::StopIteration, router_->messageBegin(metadata_));
  EXPECT_EQ(1U, context_.scope().counterFromString("test.no_healthy_upstream").value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
}

TEST_F(ThriftRouterTest, TruncatedResponse) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);
  completeRequest();

  Buffer::OwnedImpl buffer;

  EXPECT_CALL(callbacks_, startUpstreamResponse(_, _));
  EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
      .WillOnce(Return(ThriftFilters::ResponseStatus::MoreData));
  EXPECT_CALL(
      context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
      released(Ref(upstream_connection_)));
  EXPECT_CALL(callbacks_, resetDownstreamConnection());

  upstream_callbacks_->onUpstreamData(buffer, true);
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.downstream_cx_underflow_response_close")
                .value());
}

TEST_F(ThriftRouterTest, UpstreamLocalCloseMidResponse) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();

  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UpstreamCloseAfterResponse) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);
  completeRequest();

  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UpstreamDataTriggersReset) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);
  completeRequest();

  Buffer::OwnedImpl buffer;

  EXPECT_CALL(callbacks_, startUpstreamResponse(_, _));
  EXPECT_CALL(callbacks_, upstreamData(Ref(buffer)))
      .WillOnce(Return(ThriftFilters::ResponseStatus::Reset));
  EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush));

  upstream_callbacks_->onUpstreamData(buffer, true);
  destroyRouter();
}

TEST_P(ThriftRouterRainidayTest, UnexpectedUpstreamRemoteClose) {
  initializeRouter(GetParam());
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(),
                    ContainsRegex(
                        ".*connection failure before response start: remote connection failure.*"));
        EXPECT_EQ(GetParam(), end_stream);
      }));
  EXPECT_CALL(callbacks_, onReset()).Times(0);
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectFailed, _));
  router_->onEvent(Network::ConnectionEvent::RemoteClose);
}

TEST_F(ThriftRouterRainidayTest, UnexpectedUpstreamRemoteCloseCompletedRequest) {
  initializeRouter(false);
  startRequest(MessageType::Call);
  connectUpstream();
  completeRequest();

  EXPECT_CALL(callbacks_, sendLocalReply(_, false));
  EXPECT_CALL(callbacks_, onReset());
  router_->onEvent(Network::ConnectionEvent::RemoteClose);
}

// Regression test for https://github.com/envoyproxy/envoy/issues/9037.
TEST_F(ThriftRouterTest, DontCloseConnectionTwice) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::String);

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(),
                    ContainsRegex(
                        ".*connection failure before response start: remote connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  router_->onEvent(Network::ConnectionEvent::RemoteClose);

  // Connection close shouldn't happen in onDestroy(), since it's been handled.
  EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush)).Times(0);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UnexpectedRouterDestroyBeforeUpstreamConnect) {
  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_EQ(1, context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .tcp_conn_pool_.handles_.size());
  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .handles_.front(),
              cancel(Tcp::ConnectionPool::CancelPolicy::Default));
  destroyRouter();
}

TEST_F(ThriftRouterTest, UnexpectedRouterDestroy) {
  initializeRouter();
  startRequest(MessageType::Call);
  connectUpstream();
  EXPECT_CALL(upstream_connection_, close(Network::ConnectionCloseType::NoFlush));
  destroyRouter();
}

TEST_F(ThriftRouterTest, ProtocolUpgrade) {
  Stats::MockStore cluster_store;
  Stats::MockScope& cluster_scope{cluster_store.mockScope()};
  ON_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
          statsScope())
      .WillByDefault(ReturnRef(cluster_scope));

  EXPECT_CALL(cluster_store, counter("thrift.upstream_rq_call"));
  EXPECT_CALL(cluster_store, counter("thrift.upstream_resp_reply"));
  EXPECT_CALL(cluster_store, counter("thrift.upstream_resp_success"));

  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .tcp_conn_pool_.connection_data_,
              addUpstreamCallbacks(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void { upstream_callbacks_ = &cb; }));

  conn_state_.reset();
  EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .tcp_conn_pool_.connection_data_,
              connectionState())
      .WillRepeatedly(
          Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));
  EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .tcp_conn_pool_.connection_data_,
              setConnectionState_(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::ConnectionStatePtr& cs) -> void { conn_state_.swap(cs); }));

  EXPECT_CALL(*protocol_, supportsUpgrade()).WillOnce(Return(true));

  EXPECT_CALL(cluster_store,
              histogram("thrift.upstream_rq_time", Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(cluster_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_rq_time"), _));

  EXPECT_CALL(cluster_store, histogram("thrift.upstream_rq_size", Stats::Histogram::Unit::Bytes));
  EXPECT_CALL(cluster_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_rq_size"), _));
  EXPECT_CALL(cluster_store, histogram("thrift.upstream_resp_size", Stats::Histogram::Unit::Bytes));
  EXPECT_CALL(cluster_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_resp_size"), _));

  MockThriftObject* upgrade_response = new NiceMock<MockThriftObject>();

  EXPECT_CALL(*protocol_, attemptUpgrade(_, _, _))
      .WillOnce(Invoke(
          [&](Transport&, ThriftConnectionState&, Buffer::Instance& buffer) -> ThriftObjectPtr {
            buffer.add("upgrade request");
            return ThriftObjectPtr{upgrade_response};
          }));
  EXPECT_CALL(upstream_connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ("upgrade request", buffer.toString());
      }));

  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
      upstream_connection_);
  EXPECT_NE(nullptr, upstream_callbacks_);

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(*upgrade_response, onData(Ref(buffer))).WillOnce(Return(false));
  upstream_callbacks_->onUpstreamData(buffer, false);

  EXPECT_CALL(*upgrade_response, onData(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(*protocol_, completeUpgrade(_, Ref(*upgrade_response)));
  EXPECT_CALL(callbacks_, continueDecoding());
  EXPECT_CALL(*protocol_, writeMessageBegin(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
        EXPECT_EQ(metadata_->methodName(), metadata.methodName());
        EXPECT_EQ(metadata_->messageType(), metadata.messageType());
        EXPECT_EQ(metadata_->sequenceId(), metadata.sequenceId());
      }));
  upstream_callbacks_->onUpstreamData(buffer, false);

  // Then the actual request...
  sendTrivialStruct(FieldType::String);
  completeRequest();
  returnResponse();
  destroyRouter();
}

// Test the case where an upgrade will occur, but the conn pool
// returns immediately with a valid, but never, used connection.
TEST_F(ThriftRouterTest, ProtocolUpgradeOnExistingUnusedConnection) {
  initializeRouter();

  EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .tcp_conn_pool_.connection_data_,
              addUpstreamCallbacks(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void { upstream_callbacks_ = &cb; }));

  conn_state_.reset();
  EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .tcp_conn_pool_.connection_data_,
              connectionState())
      .WillRepeatedly(
          Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));
  EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .tcp_conn_pool_.connection_data_,
              setConnectionState_(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::ConnectionStatePtr& cs) -> void { conn_state_.swap(cs); }));

  MockThriftObject* upgrade_response = new NiceMock<MockThriftObject>();

  EXPECT_CALL(upstream_connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ("upgrade request", buffer.toString());
      }));

  // Simulate an existing connection that's never been used.
  EXPECT_CALL(
      context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_,
      newConnection(_))
      .WillOnce(
          Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                .newConnectionImpl(cb);

            EXPECT_CALL(*protocol_, supportsUpgrade()).WillOnce(Return(true));

            EXPECT_CALL(*protocol_, attemptUpgrade(_, _, _))
                .WillOnce(Invoke([&](Transport&, ThriftConnectionState&,
                                     Buffer::Instance& buffer) -> ThriftObjectPtr {
                  buffer.add("upgrade request");
                  return ThriftObjectPtr{upgrade_response};
                }));

            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                .poolReady(upstream_connection_);
            return nullptr;
          }));

  startRequest(MessageType::Call);

  EXPECT_NE(nullptr, upstream_callbacks_);

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(*upgrade_response, onData(Ref(buffer))).WillOnce(Return(false));
  upstream_callbacks_->onUpstreamData(buffer, false);

  EXPECT_CALL(*upgrade_response, onData(Ref(buffer))).WillOnce(Return(true));
  EXPECT_CALL(*protocol_, completeUpgrade(_, Ref(*upgrade_response)));
  EXPECT_CALL(callbacks_, continueDecoding());
  EXPECT_CALL(*protocol_, writeMessageBegin(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
        EXPECT_EQ(metadata_->methodName(), metadata.methodName());
        EXPECT_EQ(metadata_->messageType(), metadata.messageType());
        EXPECT_EQ(metadata_->sequenceId(), metadata.sequenceId());
      }));
  upstream_callbacks_->onUpstreamData(buffer, false);

  // Then the actual request...
  sendTrivialStruct(FieldType::String);
  completeRequest();
  returnResponse();
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_reply")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_success")
                .value());
}

TEST_F(ThriftRouterTest, ProtocolUpgradeSkippedOnExistingConnection) {
  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .tcp_conn_pool_.connection_data_,
              addUpstreamCallbacks(_))
      .WillOnce(Invoke(
          [&](Tcp::ConnectionPool::UpstreamCallbacks& cb) -> void { upstream_callbacks_ = &cb; }));

  conn_state_ = std::make_unique<ThriftConnectionState>();
  EXPECT_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                   .tcp_conn_pool_.connection_data_,
              connectionState())
      .WillRepeatedly(
          Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state_.get(); }));

  EXPECT_CALL(*protocol_, supportsUpgrade()).WillOnce(Return(true));

  // Protocol determines that connection state shows upgrade already occurred
  EXPECT_CALL(*protocol_, attemptUpgrade(_, _, _))
      .WillOnce(Invoke([&](Transport&, ThriftConnectionState&,
                           Buffer::Instance&) -> ThriftObjectPtr { return nullptr; }));

  EXPECT_CALL(*protocol_, writeMessageBegin(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, const MessageMetadata& metadata) -> void {
        EXPECT_EQ(metadata_->methodName(), metadata.methodName());
        EXPECT_EQ(metadata_->messageType(), metadata.messageType());
        EXPECT_EQ(metadata_->sequenceId(), metadata.sequenceId());
      }));
  EXPECT_CALL(callbacks_, continueDecoding());

  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_.poolReady(
      upstream_connection_);
  EXPECT_NE(nullptr, upstream_callbacks_);

  // Then the actual request...
  sendTrivialStruct(FieldType::String);
  completeRequest();
  returnResponse();
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_reply")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_success")
                .value());
}

TEST_F(ThriftRouterTest, PoolTimeoutUpstreamTimeMeasurement) {
  initializeRouter();

  Stats::MockStore cluster_store;
  Stats::MockScope& cluster_scope{cluster_store.mockScope()};
  ON_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
          statsScope())
      .WillByDefault(ReturnRef(cluster_scope));
  EXPECT_CALL(cluster_store, counter("thrift.upstream_rq_call"));

  startRequest(MessageType::Call);

  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_CALL(cluster_store, counter("thrift.upstream_resp_exception"));
  EXPECT_CALL(cluster_store, counter("thrift.upstream_resp_exception_local"));
  EXPECT_CALL(cluster_store, counter("thrift.upstream_resp_exception_local.timeout"));
  EXPECT_CALL(cluster_store,
              histogram("thrift.upstream_rq_time", Stats::Histogram::Unit::Milliseconds))
      .Times(0);
  EXPECT_CALL(cluster_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_rq_time"), 500))
      .Times(0);
  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(),
                    ContainsRegex(".*connection failure before response start: timeout.*"));
        EXPECT_TRUE(end_stream);
      }));
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(ConnectionPool::PoolFailureReason::Timeout);
}

TEST_P(ThriftRouterFieldTypeTest, OneWay) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Oneway);

  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess, _));
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_oneway")
                .value());
  EXPECT_EQ(0UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_reply")
                .value());
}

TEST_P(ThriftRouterFieldTypeTest, Call) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess, _));
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();

  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestSuccess, _));
  returnResponse();
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_reply")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_success")
                .value());
}

TEST_P(ThriftRouterFieldTypeTest, CallWithUpstreamRqTime) {
  FieldType field_type = GetParam();

  initializeRouter();

  Stats::MockStore cluster_store;
  Stats::MockScope& cluster_scope{cluster_store.mockScope()};
  ON_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
          statsScope())
      .WillByDefault(ReturnRef(cluster_scope));
  EXPECT_CALL(cluster_store, counter("thrift.upstream_rq_call"));
  EXPECT_CALL(cluster_store, counter("thrift.upstream_resp_reply"));
  EXPECT_CALL(cluster_store, counter("thrift.upstream_resp_success"));

  EXPECT_CALL(cluster_store, histogram("thrift.upstream_rq_size", Stats::Histogram::Unit::Bytes));
  EXPECT_CALL(cluster_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_rq_size"), _));
  EXPECT_CALL(cluster_store, histogram("thrift.upstream_resp_size", Stats::Histogram::Unit::Bytes));
  EXPECT_CALL(cluster_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_resp_size"), _));

  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();

  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_CALL(cluster_store,
              histogram("thrift.upstream_rq_time", Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(cluster_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_rq_time"), 500));
  returnResponse();
  destroyRouter();
}

TEST_P(ThriftRouterFieldTypeTest, Call_Error) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess, _));
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();

  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestFailed, _));
  returnResponse(MessageType::Reply, false);
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_reply")
                .value());
  EXPECT_EQ(0UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_success")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_error")
                .value());
}

TEST_P(ThriftRouterFieldTypeTest, Exception) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Call);

  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::LocalOriginConnectSuccess, _));
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();

  EXPECT_CALL(context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
                  .host_->outlier_detector_,
              putResult(Upstream::Outlier::Result::ExtOriginRequestFailed, _));
  returnResponse(MessageType::Exception);
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_exception_remote")
                .value());
}

TEST_P(ThriftRouterFieldTypeTest, UnknownMessageTypes) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Exception);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();
  returnResponse(MessageType::Call);
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_invalid_type")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_invalid_type")
                .value());
}

// Ensure the service name gets stripped when strip_service_name = true.
TEST_P(ThriftRouterFieldTypeTest, StripServiceNameEnabled) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Call, "Service:method", true);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();

  EXPECT_EQ("method", metadata_->methodName());

  returnResponse();
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_reply")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_success")
                .value());
}

// Ensure the service name prefix isn't stripped when strip_service_name = false.
TEST_P(ThriftRouterFieldTypeTest, StripServiceNameDisabled) {
  FieldType field_type = GetParam();

  initializeRouter();
  startRequest(MessageType::Call, "Service:method", false);
  connectUpstream();
  sendTrivialStruct(field_type);
  completeRequest();

  EXPECT_EQ("Service:method", metadata_->methodName());

  returnResponse();
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_reply")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_success")
                .value());
}

TEST_F(ThriftRouterTest, CallWithExistingConnection) {
  initializeRouter();

  // Simulate previous sequence id usage.
  conn_state_ = std::make_unique<ThriftConnectionState>(3);

  startRequestWithExistingConnection(MessageType::Call);
  sendTrivialStruct(FieldType::I32);
  completeRequest();

  EXPECT_EQ(3, metadata_->sequenceId());

  returnResponse();
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_rq_call")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_reply")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.upstream_resp_success")
                .value());
}

TEST_P(ThriftRouterContainerTest, DecoderFilterCallbacks) {
  FieldType field_type = GetParam();
  int16_t field_id = 1;

  initializeRouter();

  startRequest(MessageType::Oneway);
  connectUpstream();

  EXPECT_CALL(*protocol_, writeStructBegin(_, ""));
  EXPECT_EQ(FilterStatus::Continue, router_->structBegin({}));

  EXPECT_CALL(*protocol_, writeFieldBegin(_, "", field_type, field_id));
  EXPECT_EQ(FilterStatus::Continue, router_->fieldBegin({}, field_type, field_id));

  FieldType container_type = FieldType::I32;
  uint32_t size{};

  switch (field_type) {
  case FieldType::Map:
    size = 2;
    EXPECT_CALL(*protocol_, writeMapBegin(_, container_type, container_type, size));
    EXPECT_EQ(FilterStatus::Continue, router_->mapBegin(container_type, container_type, size));
    for (int i = 0; i < 2; i++) {
      EXPECT_CALL(*protocol_, writeInt32(_, i));
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(i));
      int j = i + 100;
      EXPECT_CALL(*protocol_, writeInt32(_, j));
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(j));
    }
    EXPECT_CALL(*protocol_, writeMapEnd(_));
    EXPECT_EQ(FilterStatus::Continue, router_->mapEnd());
    break;
  case FieldType::List:
    size = 3;
    EXPECT_CALL(*protocol_, writeListBegin(_, container_type, size));
    EXPECT_EQ(FilterStatus::Continue, router_->listBegin(container_type, size));
    for (int i = 0; i < 3; i++) {
      EXPECT_CALL(*protocol_, writeInt32(_, i));
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(i));
    }
    EXPECT_CALL(*protocol_, writeListEnd(_));
    EXPECT_EQ(FilterStatus::Continue, router_->listEnd());
    break;
  case FieldType::Set:
    size = 4;
    EXPECT_CALL(*protocol_, writeSetBegin(_, container_type, size));
    EXPECT_EQ(FilterStatus::Continue, router_->setBegin(container_type, size));
    for (int i = 0; i < 4; i++) {
      EXPECT_CALL(*protocol_, writeInt32(_, i));
      EXPECT_EQ(FilterStatus::Continue, router_->int32Value(i));
    }
    EXPECT_CALL(*protocol_, writeSetEnd(_));
    EXPECT_EQ(FilterStatus::Continue, router_->setEnd());
    break;
  default:
    PANIC("reached unexpected code");
  }

  EXPECT_CALL(*protocol_, writeFieldEnd(_));
  EXPECT_EQ(FilterStatus::Continue, router_->fieldEnd());

  EXPECT_CALL(*protocol_, writeFieldBegin(_, _, FieldType::Stop, 0));
  EXPECT_CALL(*protocol_, writeStructEnd(_));
  EXPECT_EQ(FilterStatus::Continue, router_->structEnd());

  completeRequest();
  destroyRouter();
}

TEST_P(ThriftRouterPassthroughTest, PassthroughEnable) {
  TransportType downstream_transport_type;
  ProtocolType downstream_protocol_type;
  TransportType upstream_transport_type;
  ProtocolType upstream_protocol_type;

  std::tie(downstream_transport_type, downstream_protocol_type, upstream_transport_type,
           upstream_protocol_type) = GetParam();

  constexpr absl::string_view yaml_string = R"EOF(
  transport: {}
  protocol: {}
  )EOF";

  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProtocolOptions configuration;
  TestUtility::loadFromYaml(fmt::format(yaml_string,
                                        TransportNames::get().fromType(upstream_transport_type),
                                        ProtocolNames::get().fromType(upstream_protocol_type)),
                            configuration);

  const auto protocol_option = std::make_shared<ProtocolOptionsConfigImpl>(configuration);
  EXPECT_CALL(
      *context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
      extensionProtocolOptions(_))
      .WillRepeatedly(Return(protocol_option));

  initializeRouter();
  startRequest(MessageType::Call, "method", false, downstream_transport_type,
               downstream_protocol_type);

  bool passthroughSupported = false;
  if (downstream_transport_type == upstream_transport_type &&
      downstream_transport_type == TransportType::Framed &&
      downstream_protocol_type == upstream_protocol_type &&
      downstream_protocol_type != ProtocolType::Twitter) {
    passthroughSupported = true;
  }
  ASSERT_EQ(passthroughSupported, router_->passthroughSupported());

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(app_ex.what(), ContainsRegex(".*connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));
  context_.server_factory_context_.cluster_manager_.thread_local_cluster_.tcp_conn_pool_
      .poolFailure(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
}

TEST_F(ThriftRouterTest, RequestResponseSize) {
  initializeRouter();

  Stats::MockStore cluster_store;
  expectStatCalls(cluster_store);

  startRequestWithExistingConnection(MessageType::Call);
  sendTrivialStruct(FieldType::I32);
  completeRequest();
  returnResponse();
  destroyRouter();
}

TEST_F(ThriftRouterTest, UpstreamDraining) {
  TestScopedRuntime scoped_runtime;

  initializeRouter();

  Stats::MockStore cluster_store;
  expectStatCalls(cluster_store);
  EXPECT_CALL(cluster_store, counter("thrift.upstream_cx_drain_close")).Times(AtLeast(1));
  // Keep the downstream connection.
  EXPECT_CALL(callbacks_, resetDownstreamConnection()).Times(0);
  startRequestWithExistingConnection(MessageType::Call);
  sendTrivialStruct(FieldType::I32);
  completeRequest();
  returnResponse(MessageType::Reply, true, true /* is_drain */);
  destroyRouter();
}

TEST_F(ThriftRouterTest, UpstreamPartialResponse) {
  initializeRouter();

  EXPECT_CALL(callbacks_, sendLocalReply(_, _))
      .WillOnce(Invoke([&](const DirectResponse& response, bool end_stream) -> void {
        auto& app_ex = dynamic_cast<const AppException&>(response);
        EXPECT_EQ(AppExceptionType::InternalError, app_ex.type_);
        EXPECT_THAT(
            app_ex.what(),
            ContainsRegex(
                ".*connection failure before response complete: local connection failure.*"));
        EXPECT_TRUE(end_stream);
      }));

  startRequestWithExistingConnection(MessageType::Call);
  sendTrivialStruct(FieldType::I32);
  completeRequest();
  returnResponse(MessageType::Reply, true, false, true /* is_partial*/);
  upstream_callbacks_->onEvent(Network::ConnectionEvent::LocalClose);
  destroyRouter();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("thrift.downstream_cx_partial_response_close")
                .value());
}

TEST_F(ThriftRouterTest, ShadowRequests) {
  struct ShadowClusterInfo {
    NiceMock<Upstream::MockThreadLocalCluster> cluster;
    NiceMock<Network::MockClientConnection> connection;
    Tcp::ConnectionPool::ConnectionStatePtr conn_state;
  };
  using ShadowClusterInfoPtr = std::shared_ptr<ShadowClusterInfo>;
  absl::flat_hash_map<std::string, ShadowClusterInfoPtr> shadow_clusters;

  shadow_clusters.try_emplace("shadow_cluster_1", std::make_shared<ShadowClusterInfo>());
  shadow_clusters.try_emplace("shadow_cluster_2", std::make_shared<ShadowClusterInfo>());

  for (auto& [name, shadow_cluster_info] : shadow_clusters) {
    auto& shadow_cluster = shadow_cluster_info->cluster;
    auto& upstream_connection = shadow_cluster_info->connection;
    auto& conn_state = shadow_cluster_info->conn_state;

    ON_CALL(context_.server_factory_context_.cluster_manager_,
            getThreadLocalCluster(absl::string_view(name)))
        .WillByDefault(Return(&shadow_cluster));
    EXPECT_CALL(shadow_cluster.tcp_conn_pool_, newConnection(_))
        .WillOnce(
            Invoke([&](Tcp::ConnectionPool::Callbacks& cb) -> Tcp::ConnectionPool::Cancellable* {
              shadow_cluster.tcp_conn_pool_.newConnectionImpl(cb);
              shadow_cluster.tcp_conn_pool_.poolReady(upstream_connection);
              return nullptr;
            }));
    EXPECT_CALL(upstream_connection, close(_));

    EXPECT_CALL(*shadow_cluster.tcp_conn_pool_.connection_data_, connectionState())
        .WillRepeatedly(
            Invoke([&]() -> Tcp::ConnectionPool::ConnectionState* { return conn_state.get(); }));
    EXPECT_CALL(*shadow_cluster.tcp_conn_pool_.connection_data_, setConnectionState_(_))
        .WillOnce(Invoke(
            [&](Tcp::ConnectionPool::ConnectionStatePtr& cs) -> void { conn_state.swap(cs); }));

    // Set up policies.
    envoy::type::v3::FractionalPercent default_value;
    auto policy = std::make_shared<RequestMirrorPolicyImpl>(name, "", default_value);
    route_entry_.policies_.push_back(policy);
  }

  initializeRouterWithShadowWriter();

  // Set sequence id to 0, since that's what the new connections used for shadow requests will use.
  startRequestWithExistingConnection(MessageType::Call, 0);

  std::vector<FieldType> field_types = {FieldType::Bool,  FieldType::Byte, FieldType::I16,
                                        FieldType::I32,   FieldType::I64,  FieldType::Double,
                                        FieldType::String};
  for (const auto& field_type : field_types) {
    sendTrivialStruct(field_type);
  }

  sendTrivialMap();
  sendTrivialList();
  sendTrivialSet();
  sendPassthroughData();

  completeRequest();
  returnResponse();
  destroyRouter();

  shadow_writer_impl_ = nullptr;
}

TEST_F(ThriftRouterTest, UpstreamZoneCallSuccess) {
  initializeRouter();
  initializeUpstreamZone();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::I32);
  completeRequest();
  returnResponse();

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("zone.zone_name.other_zone_name.thrift.upstream_resp_reply")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("zone.zone_name.other_zone_name.thrift.upstream_resp_success")
                .value());
  EXPECT_EQ(1UL, context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.host_->stats_.rq_success_.value());
}

TEST_F(ThriftRouterTest, UpstreamZoneCallError) {
  initializeRouter();
  initializeUpstreamZone();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::I32);
  completeRequest();
  returnResponse(MessageType::Reply, false);

  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("zone.zone_name.other_zone_name.thrift.upstream_resp_reply")
                .value());
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("zone.zone_name.other_zone_name.thrift.upstream_resp_error")
                .value());
  EXPECT_EQ(1UL, context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.host_->stats_.rq_error_.value());
}

TEST_F(ThriftRouterTest, UpstreamZoneCallException) {
  initializeRouter();
  initializeUpstreamZone();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::I32);
  completeRequest();
  returnResponse(MessageType::Exception);
  EXPECT_EQ(1UL,
            context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_
                ->statsScope()
                .counterFromString("zone.zone_name.other_zone_name.thrift.upstream_resp_exception")
                .value());
  EXPECT_EQ(1UL, context_.server_factory_context_.cluster_manager_.thread_local_cluster_
                     .tcp_conn_pool_.host_->stats_.rq_error_.value());
}

TEST_F(ThriftRouterTest, UpstreamZoneCallWithRqTime) {
  NiceMock<Stats::MockStore> cluster_store;
  Stats::MockScope& cluster_scope{cluster_store.mockScope()};
  ON_CALL(*context_.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
          statsScope())
      .WillByDefault(ReturnRef(cluster_scope));

  initializeRouter();
  initializeUpstreamZone();
  startRequest(MessageType::Call);
  connectUpstream();
  sendTrivialStruct(FieldType::I32);
  completeRequest();

  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(500));
  EXPECT_CALL(cluster_store, histogram("thrift.upstream_resp_size", Stats::Histogram::Unit::Bytes));
  EXPECT_CALL(cluster_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_resp_size"), _));

  EXPECT_CALL(cluster_store,
              histogram("thrift.upstream_rq_time", Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(cluster_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name, "thrift.upstream_rq_time"), _));

  EXPECT_CALL(cluster_store, histogram("zone.zone_name.other_zone_name.thrift.upstream_rq_time",
                                       Stats::Histogram::Unit::Milliseconds));
  EXPECT_CALL(cluster_store,
              deliverHistogramToSinks(
                  testing::Property(&Stats::Metric::name,
                                    "zone.zone_name.other_zone_name.thrift.upstream_rq_time"),
                  500));
  returnResponse();
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
