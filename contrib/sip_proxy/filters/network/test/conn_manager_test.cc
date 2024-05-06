#include <memory>

#include "source/common/buffer/buffer_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/printers.h"

#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.validate.h"
#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"
#include "contrib/sip_proxy/filters/network/source/config.h"
#include "contrib/sip_proxy/filters/network/source/conn_manager.h"
#include "contrib/sip_proxy/filters/network/source/encoder.h"
#include "contrib/sip_proxy/filters/network/test/mocks.h"
#include "contrib/sip_proxy/filters/network/test/utility.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

class TestConfigImpl : public ConfigImpl {
public:
  TestConfigImpl(envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy proto_config,
                 Server::Configuration::MockFactoryContext& context,
                 SipFilters::DecoderFilterSharedPtr decoder_filter, SipFilterStats& stats)
      : ConfigImpl(proto_config, context), decoder_filter_(decoder_filter), stats_(stats) {}

  // ConfigImpl
  SipFilterStats& stats() override { return stats_; }

  SipFilters::DecoderFilterSharedPtr custom_filter_;
  SipFilters::DecoderFilterSharedPtr decoder_filter_;
  SipFilterStats& stats_;
};

class SipConnectionManagerTest : public testing::Test {
public:
  SipConnectionManagerTest()
      : stats_(SipFilterStats::generateStats("test.", *store_.rootScope())),
        transaction_infos_(std::make_shared<Router::TransactionInfos>()) {}
  ~SipConnectionManagerTest() override {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  void initializeFilter() { initializeFilter(""); }

  void initializeFilter(const std::string& yaml) {
    // Destroy any existing filter first.
    filter_ = nullptr;

    for (const auto& counter : store_.counters()) {
      counter->reset();
    }

    if (yaml.empty()) {
      proto_config_.set_stat_prefix("test");
    } else {
      TestUtility::loadFromYaml(yaml, proto_config_);
      TestUtility::validate(proto_config_);
    }

    proto_config_.set_stat_prefix("test");

    decoder_filter_ = std::make_shared<NiceMock<SipFilters::MockDecoderFilter>>();

    config_ = std::make_shared<TestConfigImpl>(proto_config_, context_, decoder_filter_, stats_);
    EXPECT_EQ(config_->settings()->transactionTimeout(), std::chrono::milliseconds(32000));
    if (custom_filter_) {
      config_->custom_filter_ = custom_filter_;
    }

    EXPECT_CALL(context_, getTransportSocketFactoryContext())
        .WillRepeatedly(testing::ReturnRef(factory_context_));
    EXPECT_CALL(factory_context_.server_context_, localInfo())
        .WillRepeatedly(testing::ReturnRef(local_info_));
    ON_CALL(random_, random()).WillByDefault(Return(42));
    filter_ = std::make_unique<ConnectionManager>(
        config_, random_, filter_callbacks_.connection_.dispatcher_.timeSource(), context_,
        transaction_infos_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    filter_->onNewConnection();

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  void
  sendLocalReply(Envoy::Extensions::NetworkFilters::SipProxy::DirectResponse::ResponseType type) {
    const std::string yaml = R"EOF(
stat_prefix: egress
route_config:
  name: local_route
  routes:
  - match:
      domain: "pcsf-cfed.cncs.svc.cluster.local"
      header: ""
      parameter: "x-suri"
    route:
      cluster: "test"
settings:
  transaction_timeout: 32s
  local_services:
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : transport
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : x-suri
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : host
)EOF";
    initializeFilter(yaml);
    MessageMetadata metadata;
    const MockDirectResponse response;
    EXPECT_CALL(response, encode(_, _)).WillRepeatedly(Return(type));
    filter_->sendLocalReply(metadata, response, true);
  }

  void upstreamDataTest() {
    const std::string yaml = R"EOF(
stat_prefix: egress
route_config:
  name: local_route
  routes:
  - match:
      domain: "pcsf-cfed.cncs.svc.cluster.local"
      header: ""
      parameter: "x-suri"
    route:
      cluster: "test"
settings:
  transaction_timeout: 32s
  local_services:
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : transport
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : x-suri
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : host
)EOF";
    initializeFilter(yaml);

    const std::string SIP_INVITE_WRONG_CONTENT_LENGTH =
        "INVITE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
        "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
        "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
        "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
        "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
        "CSeq: 1 INVITE\x0d\x0a"
        "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
        "Supported: 100rel\x0d\x0a"
        "Route: "
        "<sip:+16959000000:15306;role=anch;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060lr;"
        "transport=udp>\x0d\x0a"
        "P-Asserted-Identity: <sip:User.0001@tas01.defult.svc.cluster.local>\x0d\x0a"
        "Allow: UPDATE,INVITE,ACK,CANCEL,BYE,PRACK,REFER,MESSAGE,INFO\x0d\x0a"
        "Max-Forwards: 70\x0d\x0a"
        "Content-Type: application/sdp\x0d\x0a"
        "Content-Length:  300\x0d\x0a"
        "\x0d\x0a"
        "v=0\x0d\x0a"
        "o=PCTEL 256 2 IN IP4 11.0.0.10\x0d\x0a"
        "c=IN IP4 11.0.0.10\x0d\x0a"
        "m=audio 4030 RTP/AVP 0 8\x0d\x0a"
        "a=rtpmap:0 PCMU/8000\x0d\x0a"
        "a=rtpmap:8 PCMU/8000\x0d\x0a";

    buffer_.add(SIP_INVITE_WRONG_CONTENT_LENGTH);

    // The "Content-Length" is larger to make reassemble do not call complete()
    filter_->decoder_->reassemble(buffer_);
    filter_->decoder_->metadata_ = std::make_shared<MessageMetadata>(buffer_.toString());
    filter_->decoder_->decode();
    ConnectionManager::ActiveTransPtr trans =
        std::make_unique<ConnectionManager::ActiveTrans>(*filter_, filter_->decoder_->metadata());
    trans->startUpstreamResponse();
    trans->upstreamData(filter_->decoder_->metadata_);

    // TransportBegin
    struct MockResponseDecoderTransportBegin : public ConnectionManager::ResponseDecoder {
      MockResponseDecoderTransportBegin(ConnectionManager::ActiveTrans& parent)
          : ConnectionManager::ResponseDecoder(parent) {}
      FilterStatus transportBegin(MessageMetadataSharedPtr) override {
        return FilterStatus::StopIteration;
      }
    };
    MockResponseDecoderTransportBegin decoder_transportBegin(*trans);
    trans->response_decoder_ =
        std::make_unique<MockResponseDecoderTransportBegin>(decoder_transportBegin);
    trans->upstreamData(filter_->decoder_->metadata_);

    // MessageBegin
    struct MockResponseDecoderMessageBegin : public ConnectionManager::ResponseDecoder {
      MockResponseDecoderMessageBegin(ConnectionManager::ActiveTrans& parent)
          : ConnectionManager::ResponseDecoder(parent) {}
      FilterStatus messageBegin(MessageMetadataSharedPtr) override {
        return FilterStatus::StopIteration;
      }
    };
    MockResponseDecoderMessageBegin decoder_messageBegin(*trans);
    trans->response_decoder_ =
        std::make_unique<MockResponseDecoderMessageBegin>(decoder_messageBegin);
    trans->upstreamData(filter_->decoder_->metadata_);

    // MessageEnd
    struct MockResponseDecoderMessageEnd : public ConnectionManager::ResponseDecoder {
      MockResponseDecoderMessageEnd(ConnectionManager::ActiveTrans& parent)
          : ConnectionManager::ResponseDecoder(parent) {}
      FilterStatus messageEnd() override { return FilterStatus::StopIteration; }
    };
    MockResponseDecoderMessageEnd decoder_messageEnd(*trans);
    trans->response_decoder_ = std::make_unique<MockResponseDecoderMessageEnd>(decoder_messageEnd);
    trans->upstreamData(filter_->decoder_->metadata_);
    EXPECT_NE(nullptr, trans->connection());

    // TransportEnd
    struct MockResponseDecoderTransportEnd : public ConnectionManager::ResponseDecoder {
      MockResponseDecoderTransportEnd(ConnectionManager::ActiveTrans& parent)
          : ConnectionManager::ResponseDecoder(parent) {}
      FilterStatus transportEnd() override { return FilterStatus::StopIteration; }
    };
    MockResponseDecoderTransportEnd decoder_transportEnd(*trans);
    trans->response_decoder_ =
        std::make_unique<MockResponseDecoderTransportEnd>(decoder_transportEnd);
    trans->upstreamData(filter_->decoder_->metadata_);
    filter_->continueHandling(filter_->decoder_->metadata_,
                              filter_->newDecoderEventHandler(filter_->decoder_->metadata()));

    // AppException
    struct MockResponseDecoderAppException : public ConnectionManager::ResponseDecoder {
      MockResponseDecoderAppException(ConnectionManager::ActiveTrans& parent)
          : ConnectionManager::ResponseDecoder(parent) {}
      FilterStatus transportBegin(MessageMetadataSharedPtr) override {
        throw AppException(AppExceptionType::ProtocolError, "MockResponseDecoderAppException");
      }
    };
    MockResponseDecoderAppException decoder_appException(*trans);
    trans->response_decoder_ =
        std::make_unique<MockResponseDecoderAppException>(decoder_appException);
    try {
      trans->upstreamData(filter_->decoder_->metadata_);
    } catch (const EnvoyException& ex) {
      filter_->stats_.response_exception_.inc();
      EXPECT_EQ(1U, filter_->stats_.response_exception_.value());
    }

    // EnvoyException
    struct MockResponseDecoderEnvoyException : public ConnectionManager::ResponseDecoder {
      MockResponseDecoderEnvoyException(ConnectionManager::ActiveTrans& parent)
          : ConnectionManager::ResponseDecoder(parent) {}
      FilterStatus transportBegin(MessageMetadataSharedPtr) override {
        throw EnvoyException("MockResponseDecoderEnvoyException");
      }
    };
    MockResponseDecoderEnvoyException decoder_envoyException(*trans);
    trans->response_decoder_ =
        std::make_unique<MockResponseDecoderEnvoyException>(decoder_envoyException);
    try {
      trans->upstreamData(filter_->decoder_->metadata_);
    } catch (const EnvoyException& ex) {
      filter_->stats_.response_exception_.inc();
      EXPECT_EQ(2U, filter_->stats_.response_exception_.value());
    }

    // transportEnd throw envoyException
    filter_->decoder_->reassemble(buffer_);
    filter_->decoder_->metadata_ = std::make_shared<MessageMetadata>(buffer_.toString());
    filter_->decoder_->decode();
    filter_->read_callbacks_->connection().setDelayedCloseTimeout(std::chrono::milliseconds(1));
    filter_->read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
    ConnectionManager::ActiveTransPtr trans1 =
        std::make_unique<ConnectionManager::ActiveTrans>(*filter_, filter_->decoder_->metadata());
    try {
      ConnectionManager::ResponseDecoder response_decoder(*trans1);
      response_decoder.newDecoderEventHandler(filter_->decoder_->metadata());
      // transportEnd throw envoyException
      response_decoder.onData(filter_->decoder_->metadata());
    } catch (const EnvoyException& ex) {
      filter_->stats_.response_exception_.inc();
      EXPECT_EQ(4U, filter_->stats_.response_exception_.value());
    }

    // end_stream = false
    ConnectionManager::ActiveTransPtr trans2 =
        std::make_unique<ConnectionManager::ActiveTrans>(*filter_, filter_->decoder_->metadata());
    trans2->sendLocalReply(AppException(AppExceptionType::ProtocolError, "End_stream is false"),
                           false);

    // route() with metadata=nullptr;
    ConnectionManager::ActiveTransPtr trans3 =
        std::make_unique<ConnectionManager::ActiveTrans>(*filter_, filter_->decoder_->metadata());
    trans3->metadata_ = nullptr;
    EXPECT_EQ(nullptr, trans3->route());

    trans3->resetDownstreamConnection();
  }

  void resetAllTransTest(bool local_reset) {
    // int before = stats_.cx_destroy_local_with_active_rq_;
    const std::string yaml = R"EOF(
stat_prefix: egress
route_config:
  name: local_route
  routes:
  - match:
      domain: "pcsf-cfed.cncs.svc.cluster.local"
      header: "Route"
      parameter: "x-suri"
    route:
      cluster: "test"
settings:
  transaction_timeout: 32s
  local_services:
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : transport
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : x-suri
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : host
)EOF";
    initializeFilter(yaml);

    const std::string SIP_ACK_FULL =
        "ACK sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
        "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
        "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
        "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
        "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
        "CSeq: 1 ACK\x0d\x0a"
        "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
        "Supported: 100rel\x0d\x0a"
        "Route: "
        "<sip:+16959000000:15306;role=anch;lr;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;"
        "transport=udp>\x0d\x0a"
        "P-Asserted-Identity: <sip:User.0001@tas01.defult.svc.cluster.local>\x0d\x0a"
        "Allow: UPDATE,INVITE,ACK,CANCEL,BYE,PRACK,REFER,MESSAGE,INFO\x0d\x0a"
        "Max-Forwards: 70\x0d\x0a"
        "Content-Type: application/sdp\x0d\x0a"
        "Content-Length:  127\x0d\x0a"
        "\x0d\x0a";
    buffer_.add(SIP_ACK_FULL);

    filter_->decoder_->reassemble(buffer_);
    filter_->decoder_->metadata_ = std::make_shared<MessageMetadata>(buffer_.toString());
    filter_->decoder_->decode();

    MessageMetadataSharedPtr metadata = filter_->decoder_->metadata_;
    std::string&& k = std::string(metadata->transactionId().value());
    ConnectionManager::ActiveTransPtr new_trans =
        std::make_unique<ConnectionManager::ActiveTrans>(*filter_, metadata);
    new_trans->createFilterChain();
    filter_->transactions_.emplace(k, std::move(new_trans));
    filter_->newDecoderEventHandler(metadata);
    filter_->resetAllTrans(local_reset);
  }

  void resumeResponseTest() {
    const std::string yaml = R"EOF(
stat_prefix: egress
route_config:
  name: local_route
  routes:
  - match:
      domain: "pcsf-cfed.cncs.svc.cluster.local"
      header: "Route"
      parameter: "x-suri"
    route:
      cluster: "test"
settings:
  transaction_timeout: 32s
  local_services:
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : transport
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : x-suri
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : host
)EOF";
    initializeFilter(yaml);

    const std::string SIP_ACK_FULL =
        "ACK sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
        "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
        "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
        "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
        "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
        "CSeq: 1 ACK\x0d\x0a"
        "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
        "Supported: 100rel\x0d\x0a"
        "Route: "
        "<sip:+16959000000:15306;role=anch;lr;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;"
        "transport=udp>\x0d\x0a"
        "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
        "P-Asserted-Identity: <sip:User.0001@tas01.defult.svc.cluster.local>\x0d\x0a"
        "Allow: UPDATE,INVITE,ACK,CANCEL,BYE,PRACK,REFER,MESSAGE,INFO\x0d\x0a"
        "Max-Forwards: 70\x0d\x0a"
        "Content-Type: application/sdp\x0d\x0a"
        "Content-Length:  127\x0d\x0a"
        "\x0d\x0a";
    buffer_.add(SIP_ACK_FULL);

    filter_->decoder_->reassemble(buffer_);
    filter_->decoder_->metadata_ = std::make_shared<MessageMetadata>(buffer_.toString());
    filter_->decoder_->decode();

    MessageMetadataSharedPtr metadata = filter_->decoder_->metadata_;
    ConnectionManager::ActiveTransPtr new_trans =
        std::make_unique<ConnectionManager::ActiveTrans>(*filter_, metadata);

    new_trans->filter_action_ = [&](DecoderEventHandler* filter) -> FilterStatus {
      UNREFERENCED_PARAMETER(filter);
      new_trans->local_response_sent_ = true;
      return FilterStatus::StopIteration;
    };

    std::list<ConnectionManager::ActiveTransDecoderFilterPtr> decoder_filter_list;
    ConnectionManager::ActiveTransDecoderFilterPtr wrapper =
        std::make_unique<ConnectionManager::ActiveTransDecoderFilter>(*new_trans, decoder_filter_);
    decoder_filter_->setDecoderFilterCallbacks(*wrapper);
    LinkedList::moveIntoListBack(std::move(wrapper), decoder_filter_list);

    std::shared_ptr<SipFilters::MockDecoderFilter> decoder_filter_1 =
        std::make_shared<NiceMock<SipFilters::MockDecoderFilter>>();
    ConnectionManager::ActiveTransDecoderFilterPtr wrapper2 =
        std::make_unique<ConnectionManager::ActiveTransDecoderFilter>(*new_trans, decoder_filter_1);
    LinkedList::moveIntoListBack(std::move(wrapper2), decoder_filter_list);

    new_trans->applyDecoderFilters((*(decoder_filter_list.begin())).get());

    // Other ActiveTransDecoderFilter  function cover
    ConnectionManager::ActiveTransDecoderFilterPtr decoder =
        std::make_unique<ConnectionManager::ActiveTransDecoderFilter>(*new_trans, decoder_filter_);
    EXPECT_EQ(decoder->streamId(), new_trans->streamId());
    EXPECT_EQ(decoder->connection(), new_trans->connection());
    decoder->startUpstreamResponse();
    decoder->streamInfo();
    decoder->upstreamData(metadata);
    decoder->resetDownstreamConnection();
    filter_->transactions_.emplace(std::string(metadata->transactionId().value()),
                                   std::move(new_trans));
    decoder->onReset();
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  std::shared_ptr<SipFilters::MockDecoderFilter> decoder_filter_;
  Stats::TestUtil::TestStore store_;
  SipFilterStats stats_;
  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy proto_config_;

  std::shared_ptr<TestConfigImpl> config_;

  Buffer::OwnedImpl buffer_;
  Buffer::OwnedImpl write_buffer_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Random::MockRandomGenerator> random_;
  std::unique_ptr<ConnectionManager> filter_;
  std::shared_ptr<Router::TransactionInfos> transaction_infos_;
  SipFilters::DecoderFilterSharedPtr custom_filter_;
  MessageMetadataSharedPtr metadata_;
};

TEST_F(SipConnectionManagerTest, OnDataHandlesSipCall) {
  const std::string yaml = R"EOF(
stat_prefix: egress
route_config:
  name: local_route
  routes:
  - match:
      domain: "pcsf-cfed.cncs.svc.cluster.local"
      header: "Route"
      parameter: "x-suri"
    route:
      cluster: "test"
settings:
  transaction_timeout: 32s
  local_services:
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : transport
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : x-suri
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : host
)EOF";
  initializeFilter(yaml);

  const std::string SIP_INVITE_FULL =
      "INVITE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.11:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 1 INVITE\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Supported: 100rel\x0d\x0a"
      "Route: "
      "<sip:+16959000000:15306;role=anch;lr;transport=udp;x-suri=sip:pcsf-cfed.cncs.svc.cluster."
      "local:5060>\x0d\x0a"
      "P-Asserted-Identity: <sip:User.0001@tas01.defult.svc.cluster.local>\x0d\x0a"
      "Allow: UPDATE,INVITE,ACK,CANCEL,BYE,PRACK,REFER,MESSAGE,INFO\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Type: application/sdp\x0d\x0a"
      "Content-Length:  127\x0d\x0a"
      "\x0d\x0a"
      "v=0\x0d\x0a"
      "o=PCTEL 256 2 IN IP4 11.0.0.10\x0d\x0a"
      "c=IN IP4 11.0.0.10\x0d\x0a"
      "m=audio 4030 RTP/AVP 0 8\x0d\x0a"
      "a=rtpmap:0 PCMU/8000\x0d\x0a"
      "a=rtpmap:8 PCMU/8000\x0d\x0a";

  buffer_.add(SIP_INVITE_FULL);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(filter_->onData(buffer_, true), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());

  const std::string SIP_ACK_FULL =
      "ACK sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 1 ACK\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Supported: 100rel\x0d\x0a"
      "Route: "
      "<sip:+16959000000:15306;role=anch;lr;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;"
      "transport=udp>\x0d\x0a"
      "P-Asserted-Identity: <sip:User.0001@tas01.defult.svc.cluster.local>\x0d\x0a"
      "Allow: UPDATE,INVITE,ACK,CANCEL,BYE,PRACK,REFER,MESSAGE,INFO\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Type: application/sdp\x0d\x0a"
      "Content-Length:  127\x0d\x0a"
      "\x0d\x0a";
  write_buffer_.add(SIP_ACK_FULL);
  EXPECT_EQ(filter_->onData(write_buffer_, false), Network::FilterStatus::StopIteration);
}

TEST_F(SipConnectionManagerTest, OnDataHandlesSipCallDefaultMatch) {
  const std::string yaml = R"EOF(
stat_prefix: egress
route_config:
  name: local_route
  routes:
  - match:
      domain: "pcsf-cfed.cncs.svc.cluster.local"
    route:
      cluster: "test"
settings:
  transaction_timeout: 32s
  local_services:
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : transport
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : x-suri
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : host
)EOF";
  initializeFilter(yaml);

  const std::string SIP_INVITE_FULL =
      "INVITE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.11:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 1 INVITE\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Supported: 100rel\x0d\x0a"
      "Route: "
      "<sip:pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri=sip:pcsf-cfed.cncs."
      "svc.cluster.local:5060>\x0d\x0a"
      "P-Asserted-Identity: <sip:User.0001@tas01.defult.svc.cluster.local>\x0d\x0a"
      "Allow: UPDATE,INVITE,ACK,CANCEL,BYE,PRACK,REFER,MESSAGE,INFO\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Type: application/sdp\x0d\x0a"
      "Content-Length:  127\x0d\x0a"
      "\x0d\x0a"
      "v=0\x0d\x0a"
      "o=PCTEL 256 2 IN IP4 11.0.0.10\x0d\x0a"
      "c=IN IP4 11.0.0.10\x0d\x0a"
      "m=audio 4030 RTP/AVP 0 8\x0d\x0a"
      "a=rtpmap:0 PCMU/8000\x0d\x0a"
      "a=rtpmap:8 PCMU/8000\x0d\x0a";

  buffer_.add(SIP_INVITE_FULL);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipConnectionManagerTest, OnDataHandlesSipCallEndStream) {
  const std::string yaml = R"EOF(
stat_prefix: egress
route_config:
  name: local_route
  routes:
  - match:
      domain: "pcsf-cfed.cncs.svc.cluster.local"
      header: "Record-Route"
      parameter: "x-suri"
    route:
      cluster: "test"
settings:
  transaction_timeout: 32s
  local_services:
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : transport
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : x-suri
  - domain: pcsf-cfed.cncs.svc.cluster.local
    parameter : host
)EOF";
  initializeFilter(yaml);

  const std::string SIP_INVITE_FULL =
      "INVITE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 1 INVITE\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Supported: 100rel\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Record-Route: "
      "<sip:+16959000000:15306;role=anch;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;lr;"
      "transport=udp>\x0d\x0a"
      "P-Asserted-Identity: <sip:User.0001@tas01.defult.svc.cluster.local>\x0d\x0a"
      "Allow: UPDATE,INVITE,ACK,CANCEL,BYE,PRACK,REFER,MESSAGE,INFO\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Type: application/sdp\x0d\x0a"
      "Content-Length:  127\x0d\x0a"
      "\x0d\x0a"
      "v=0\x0d\x0a"
      "o=PCTEL 256 2 IN IP4 11.0.0.10\x0d\x0a"
      "c=IN IP4 11.0.0.10\x0d\x0a"
      "m=audio 4030 RTP/AVP 0 8\x0d\x0a"
      "a=rtpmap:0 PCMU/8000\x0d\x0a"
      "a=rtpmap:8 PCMU/8000\x0d\x0a";

  buffer_.add(SIP_INVITE_FULL);

  EXPECT_EQ(filter_->onData(buffer_, true), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipConnectionManagerTest, ContinueHandling) {
  initializeFilter();
  filter_->continueHandling("10.0.0.1");
}

TEST_F(SipConnectionManagerTest, SendLocalReply_SuccessReply) {
  sendLocalReply(
      Envoy::Extensions::NetworkFilters::SipProxy::DirectResponse::ResponseType::SuccessReply);
}

TEST_F(SipConnectionManagerTest, SendLocalReply_ErrorReply) {
  sendLocalReply(
      Envoy::Extensions::NetworkFilters::SipProxy::DirectResponse::ResponseType::ErrorReply);
}

TEST_F(SipConnectionManagerTest, SendLocalReply_Exception) {
  sendLocalReply(
      Envoy::Extensions::NetworkFilters::SipProxy::DirectResponse::ResponseType::Exception);
}

TEST_F(SipConnectionManagerTest, UpstreamData) { upstreamDataTest(); }

TEST_F(SipConnectionManagerTest, ResetLocalTrans) {
  resetAllTransTest(true);
  EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());
}

TEST_F(SipConnectionManagerTest, ResetRemoteTrans) {
  resetAllTransTest(false);
  EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
}
TEST_F(SipConnectionManagerTest, ResumeResponse) { resumeResponseTest(); }

TEST_F(SipConnectionManagerTest, EncodeInsertOpaque) {
  const std::string SIP_OK200_FULL =
      "SIP/2.0 200 OK\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 1 INVITE\x0d\x0a"
      "Contact: "
      "<sip:User.0001@11.0.0.10:15060;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;transport="
      "TCP>\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";

  buffer_.add(SIP_OK200_FULL);

  absl::string_view header =
      "Contact: <sip:User.0001@11.0.0.10:15060;x-suri=sip:pcsf-cfed.cncs.svc.cluster."
      "local:5060;transport=TCP>";
  metadata_ = std::make_shared<MessageMetadata>(buffer_.toString());
  metadata_->addOpaqueOperation(SIP_OK200_FULL.find("Contact: "), header);
  Buffer::OwnedImpl response_buffer;
  metadata_->setEP("127.0.0.1");

  std::shared_ptr<EncoderImpl> encoder = std::make_shared<EncoderImpl>();
  encoder->encode(metadata_, response_buffer);
  EXPECT_EQ(response_buffer.length(), buffer_.length() + strlen(",opaque=\"127.0.0.1\""));
}

TEST_F(SipConnectionManagerTest, EncodeInsert) {
  const std::string SIP_OK200_FULL =
      "SIP/2.0 200 OK\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 1 INVITE\x0d\x0a"
      "Contact: "
      "<sip:User.0001@11.0.0.10:15060;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;transport="
      "TCP>\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";

  buffer_.add(SIP_OK200_FULL);

  metadata_ = std::make_shared<MessageMetadata>(buffer_.toString());
  metadata_->setOperation(Operation(OperationType::Insert,
                                    SIP_OK200_FULL.find(";transport=TCP") + strlen(";transport="),
                                    InsertOperationValue(";ep=10.0.0.1")));
  Buffer::OwnedImpl response_buffer;

  std::shared_ptr<EncoderImpl> encoder = std::make_shared<EncoderImpl>();
  encoder->encode(metadata_, response_buffer);
  EXPECT_EQ(response_buffer.length(), buffer_.length() + strlen(";ep=10.0.0.1"));
}

TEST_F(SipConnectionManagerTest, EncodeDelete) {
  const std::string SIP_OK200_FULL =
      "SIP/2.0 200 OK\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 1 INVITE\x0d\x0a"
      "Contact: "
      "<sip:User.0001@11.0.0.10:15060;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;transport="
      "TCP>\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";

  buffer_.add(SIP_OK200_FULL);

  metadata_ = std::make_shared<MessageMetadata>(buffer_.toString());
  metadata_->setOperation(Operation(OperationType::Delete, SIP_OK200_FULL.find(";transport="),
                                    DeleteOperationValue(strlen(";transport=TCP"))));
  Buffer::OwnedImpl response_buffer;

  std::shared_ptr<EncoderImpl> encoder = std::make_shared<EncoderImpl>();
  encoder->encode(metadata_, response_buffer);
  EXPECT_EQ(response_buffer.length(), buffer_.length() - strlen(";transport=TCP"));
}

TEST_F(SipConnectionManagerTest, EncodeModify) {
  const std::string SIP_OK200_FULL =
      "SIP/2.0 200 OK\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 1 INVITE\x0d\x0a"
      "Contact: "
      "<sip:User.0001@11.0.0.10:15060;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;transport="
      "TCP>\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";

  buffer_.add(SIP_OK200_FULL);

  metadata_ = std::make_shared<MessageMetadata>(buffer_.toString());
  metadata_->setOperation(Operation(OperationType::Modify,
                                    SIP_OK200_FULL.find(";transport=") + strlen(";transport="),
                                    ModifyOperationValue(strlen("TCP"), "SCTP")));
  Buffer::OwnedImpl response_buffer;

  std::shared_ptr<EncoderImpl> encoder = std::make_shared<EncoderImpl>();
  encoder->encode(metadata_, response_buffer);
  EXPECT_EQ(response_buffer.length(), buffer_.length() - strlen("TCP") + strlen("SCTP"));
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
