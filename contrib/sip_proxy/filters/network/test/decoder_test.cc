#include "source/common/buffer/buffer_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"
#include "contrib/sip_proxy/filters/network/source/config.h"
#include "contrib/sip_proxy/filters/network/source/conn_manager.h"
#include "contrib/sip_proxy/filters/network/source/decoder.h"
#include "contrib/sip_proxy/filters/network/source/encoder.h"
#include "contrib/sip_proxy/filters/network/test/mocks.h"
#include "contrib/sip_proxy/filters/network/test/utility.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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
  void createFilterChain(SipFilters::FilterChainFactoryCallbacks& callbacks) override {
    if (custom_filter_) {
      callbacks.addDecoderFilter(custom_filter_);
    }
    callbacks.addDecoderFilter(decoder_filter_);
  }

  SipFilters::DecoderFilterSharedPtr custom_filter_;
  SipFilters::DecoderFilterSharedPtr decoder_filter_;
  SipFilterStats& stats_;
};

class SipDecoderTest : public testing::Test {
public:
  SipDecoderTest()
      : stats_(SipFilterStats::generateStats("test.", store_)),
        transaction_infos_(std::make_shared<Router::TransactionInfos>()) {}
  ~SipDecoderTest() override {
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

    config_ = std::make_unique<TestConfigImpl>(proto_config_, context_, decoder_filter_, stats_);
    if (custom_filter_) {
      config_->custom_filter_ = custom_filter_;
    }

    ON_CALL(random_, random()).WillByDefault(Return(42));
    filter_ = std::make_unique<ConnectionManager>(
        *config_, random_, filter_callbacks_.connection_.dispatcher_.timeSource(), context_,
        transaction_infos_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    filter_->onNewConnection();

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  void headerHandlerTest() {
    MockDecoderCallbacks callback;
    Decoder decoder(callback);
    decoder.setCurrentHeader(HeaderType::Via);
    Decoder::REGISTERHandler msgHandler(decoder);
    Decoder::HeaderHandler headerHandler(msgHandler);
    EXPECT_EQ(HeaderType::Via, headerHandler.currentHeader());

    DecoderStateMachine::DecoderStatus status(State::MessageBegin);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<SipFilters::MockDecoderFilter> decoder_filter_;
  Stats::TestUtil::TestStore store_;
  SipFilterStats stats_;
  envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy proto_config_;

  std::unique_ptr<TestConfigImpl> config_;

  Buffer::OwnedImpl buffer_;
  Buffer::OwnedImpl write_buffer_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Random::MockRandomGenerator> random_;
  std::unique_ptr<ConnectionManager> filter_;
  std::shared_ptr<Router::TransactionInfos> transaction_infos_;
  SipFilters::DecoderFilterSharedPtr custom_filter_;
};

const std::string yaml = R"EOF(
stat_prefix: egress
route_config:
  name: local_route
  routes:
  - match:
      domain: "test"
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

TEST_F(SipDecoderTest, DecodeINVITE) {
  initializeFilter(yaml);

  const std::string SIP_INVITE_FULL =
      "INVITE sip:User.0000@tas01.defult.svc.cluster.local;ep=127.0.0.1 SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Route: "
      "<sip:+16959000000:15306;role=anch;lr;transport=udp;x-suri=sip:pcsf-cfed.cncs.svc.cluster."
      "local:5060>\x0d\x0a"
      "Record-Route: "
      "<sip:+16959000000:15306;role=anch;lr;transport=udp;x-suri=sip:pcsf-cfed.cncs.svc.cluster."
      "local:5060>\x0d\x0a"
      "Service-Route: "
      "<sip:test@pcsf-cfed.cncs.svc.cluster.local;role=anch;lr;transport=udp;x-suri=sip:scsf-cfed."
      "cncs.svc.cluster.local:5060>\x0d\x0a"
      "Path: "
      "<sip:10.177.8.232;x-fbi=cfed;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip=192."
      "169.110.53;lr;ottag=ue_term;bidx=563242011197570;access-type=ADSL;x-alu-prset-id>\x0d\x0a"
      "CSeq: 1 INVITE\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "P-Charging-Vector: orig-ioi=ims.com;term-ioi= ims.com\x0d\x0a"
      "P-Charging-Vector: orig-ioi=ims1.com;term-ioi= ims1.com\x0d\x0a"
      "P-Charging-Function-Addresses: ccf=0.0.0.0\x0d\x0a"
      "P-Nokia-Cookie-IP-Mapping: S1F1=10.0.0.1\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";

  buffer_.add(SIP_INVITE_FULL);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  Buffer::OwnedImpl response_buffer;

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeRegister) {
  initializeFilter(yaml);

  const std::string SIP_REGISTER_FULL =
      "REGISTER sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "CSeq: 1 REGISTER\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Expires: 7200\x0d\x0a"
      "Supported: 100rel,timer\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "Require: Path\x0d\x0a"
      "Path: "
      "<sip:10.177.8.232;x-fbi=cfed;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip=192."
      "169.110.53;lr;ottag=ue_term;bidx=563242011197570;access-type=ADSL;x-alu-prset-id>\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Service-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Authorization: Digest username=\"tc05sub1@cncs.nokialab.com\", realm=\"cncs.nokialab.com\", "
      "nonce=\"436dbd0f60a52adc2DPadc43f91c774b51ac4cad614258c43cf9df\", algorithm=MD5, "
      "uri=\"sip:10.30.29.47\", response=\"c4f3c2fccdca9c5febc66d4226b5afae\", nc=01201201, "
      "cnonce=\"123456\", qop=auth\x0d\x0a"
      "Authorization: Digest username=\"tc05sub1@cncs.nokialab.com\", realm=\"cncs.nokialab.com\", "
      "nonce=\"436dbd0f60a52adc2DPadc43f91c774b51ac4cad614258c43cf9df\", algorithm=MD5, "
      "uri=\"sip:10.30.29.47\", response=\"c4f3c2fccdca9c5febc66d4226b5afae\", nc=01201201, "
      "cnonce=\"123456\", qop=auth, opaque=\"127.0.0.1\"\x0d\x0a"
      "Authorization: Digest username=\"tc05sub1@cncs.nokialab.com\", realm=\"cncs.nokialab.com\", "
      "nonce=\"436dbd0f60a52adc2DPadc43f91c774b51ac4cad614258c43cf9df\", algorithm=MD5, "
      "uri=\"sip:10.30.29.47\", response=\"c4f3c2fccdca9c5febc66d4226b5afae\", nc=01201201, "
      "cnonce=\"123456\", qop=auth, opaque=\"127.0.0.1\x0d\x0a"
      "P-Nokia-Cookie-IP-Mapping: S1F1=10.0.0.1\x0d\x0a"
      "\x0d\x0a";

  buffer_.add(SIP_REGISTER_FULL);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeOK200) {
  initializeFilter(yaml);

  const std::string SIP_OK200_FULL =
      "SIP/2.0 200 OK\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 1 INVITE\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Service-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Path: "
      "<sip:10.177.8.232;x-fbi=cfed;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip=192."
      "169.110.53;lr;ottag=ue_term;bidx=563242011197570;access-type=ADSL;x-alu-prset-id>\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_OK200_FULL);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  const std::string SIP_OK200_REGISTER =
      "SIP/2.0 200 OK\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 1 Register\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Service-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_OK200_REGISTER);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(2U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeOK200EmptyHeader) {
  initializeFilter(yaml);

  const std::string SIP_OK200_EMPTY_HEADER =
      "SIP/2.0 200 OK\x0d\x0a"
      "Via:SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "From:<sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "To:<sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "Call-ID:1-3193@11.0.0.10\x0d\x0a"
      "CSeq:100 REGISTER\x0d\x0a"
      "Session-ID:fdc85ff600804114a90b50e04de2b988;remote=b74c76b50080450dabd12317fb6b8aa7\x0d\x0a"
      "User-Agent:test-client-v1.1\x0d\x0a"
      "Supported:\x0d\x0a"
      "Contact:<sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Content-Length:0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_OK200_EMPTY_HEADER);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeOK200DifferentHeaderFormats) {
  initializeFilter(yaml);

  const std::string SIP_OK200_DIFF_HEADER_FORMATS =
      "SIP/2.0 200 OK\x0d\x0a"
      "Via:             SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "From          :<sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "To      :           <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "Call-ID:1-3193@11.0.0.10\x0d\x0a"
      "CSeq: 100 REGISTER\x0d\x0a"
      "Session-ID:   fdc85ff600804114ae04de2b988;remote=b74c76b50050dabd12317fb6b8aa7\x0d\x0a"
      "User-Agent  :    test-client-v1.1\x0d\x0a"
      "Supported   :\x0d\x0a"
      "Contact  :  <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Content-Length: 0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_OK200_DIFF_HEADER_FORMATS);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeGeneral) {
  initializeFilter(yaml);

  const std::string SIP_CANCEL_FULL =
      "CANCEL sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp;ep=cfed>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp;ep=cfed>\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 1 CANCEL\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Path: "
      "<sip:10.177.8.232;x-fbi=cfed;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip=192."
      "169.110.53;lr;ottag=ue_term;bidx=563242011197570;access-type=ADSL;x-alu-prset-id>\x0d\x0a"
      "P-Nokia-Cookie-IP-Mapping: S1F1=10.0.0.1\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";

  buffer_.add(SIP_CANCEL_FULL);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeSUBSCRIBE) {
  initializeFilter(yaml);

  const std::string SIP_SUBSCRIBE_FULL =
      "SUBSCRIBE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp;ep=cfed>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 2 SUBSCRIBE\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "P-Nokia-Cookie-IP-Mapping: S1F1=10.0.0.1\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "Event: feature-status-exchange\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_SUBSCRIBE_FULL);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  const std::string SIP_SUBSCRIBE_REG =
      "SUBSCRIBE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp;ep=cfed>\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 2 SUBSCRIBE\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "Event: reg\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_SUBSCRIBE_REG);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(2U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeFAILURE4XX) {
  initializeFilter(yaml);

  const std::string SIP_FAILURE4XX_FULL =
      "SIP/2.0 401 Unauthorized\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>\x0d\x0a"
      "Via: SIP/2.0/TCP "
      "192.169.110.15:5060;branch=z9hG4bK1f6eb66cd87d2ae67c4b8a69d67c4f7e60a522a8-0-b-"
      "60a52adb349a8674\x0d\x0a"
      "Via: SIP/2.0/UDP 127.0.0.1;branch=z9hG4bK_0002_34-139705093266412;lsstag=pt-1.12\x0d\x0a"
      "Via: SIP/2.0/UDP 10.30.29.58:38612;received=10.30.29.58;branch=z9hG4bK1434\x0d\x0a"
      "From: <sip:tc05sub1@cncs.nokialab.com>;tag=587215\x0d\x0a"
      "To: <sip:tc05sub1@cncs.nokialab.com>;tag=182294901\x0d\x0a"
      "Call-ID: tc05sub1-1@10.30.29.58-38612\x0d\x0a"
      "CSeq: 6 REGISTER\x0d\x0a"
      "P-Charging-Vector: icid-value=\"PCSF:1-cfed-0-1-0000000060a52adb-000000000000000b\"\x0d\x0a"
      "WWW-Authenticate: Digest "
      "realm=\"cncs.nokialab.com\",nonce="
      "\"436dbd0f60a52adc2DPadc43f91c774b51ac4cad614258c43cf9df\",algorithm=MD5,qop="
      "\"auth\"\x0d\x0a"
      "WWW-Authenticate: Digest "
      "realm=\"cncs.nokialab.com\",nonce="
      "\"436dbd0f60a52adc2DPadc43f91c774b51ac4cad614258c43cf9df\",algorithm=MD5,qop=\"auth\","
      "opaque=\"127.0.0.1\"\x0d\x0a"
      "WWW-Authenticate: Digest "
      "realm=\"cncs.nokialab.com\",nonce="
      "\"436dbd0f60a52adc2DPadc43f91c774b51ac4cad614258c43cf9df\",algorithm=MD5,qop=\"auth\","
      "opaque=\"127.0.0.1\x0d\x0a"
      "P-Nokia-Cookie-IP-Mapping: S1F1=10.0.0.1\x0d\x0a"
      "Content-Length: 0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_FAILURE4XX_FULL);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeEMPTY) {
  initializeFilter(yaml);

  const std::string SIP_EMPTY = "\x0d\x0a";
  buffer_.add(SIP_EMPTY);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  const std::string SIP_WRONG_METHOD_TYPE =
      "WRONGMETHOD sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 2 SUBSCRIBE\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>;tag=1\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_WRONG_METHOD_TYPE);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  const std::string SIP_NO_CONTENT_LENGTH =
      "ACK sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 2 ACK\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>;tag=1\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_NO_CONTENT_LENGTH);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  const std::string SIP_CONTENT_LENGTH_ZERO =
      "ACK sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 2 ACK\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>;tag=1\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Length:  -1\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_NO_CONTENT_LENGTH);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeAck) {
  initializeFilter(yaml);

  const std::string SIP_ACK_FULL =
      "ACK sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 2 ACK\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>;tag=1\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_ACK_FULL);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeBYE) {
  initializeFilter(yaml);

  const std::string SIP_BYE_FULL =
      "BYE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 2 BYE\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>;tag=1\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_BYE_FULL);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeUPDATE) {
  initializeFilter(yaml);

  const std::string SIP_UPDATE_FULL =
      "UPDATE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 2 UPDATE\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>;tag=1\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_UPDATE_FULL);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeREFER) {
  initializeFilter(yaml);

  const std::string SIP_REFER_FULL =
      "REFER sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 2 REFER\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>;tag=1\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_REFER_FULL);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, DecodeNOTIFY) {
  initializeFilter(yaml);

  const std::string SIP_NOTIFY_FULL =
      "NOTIFY sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0\x0d\x0a"
      "Call-ID: 1-3193@11.0.0.10\x0d\x0a"
      "Via: SIP/2.0/TCP 11.0.0.10:15060;branch=z9hG4bK-3193-1-0\x0d\x0a"
      "To: <sip:User.0000@tas01.defult.svc.cluster.local>\x0d\x0a"
      "From: <sip:User.0001@tas01.defult.svc.cluster.local>;tag=1\x0d\x0a"
      "Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "CSeq: 1 NOTIFY\x0d\x0a"
      "Contact: <sip:User.0001@11.0.0.10:15060;transport=TCP>;tag=1\x0d\x0a"
      "Record-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Service-Route: <sip:+16959000000:15306;role=anch;lr;transport=udp>\x0d\x0a"
      "Path: "
      "<sip:10.177.8.232;x-fbi=cfed;x-suri=sip:pcsf-cfed.cncs.svc.cluster.local:5060;inst-ip=192."
      "169.110.53;lr;ottag=ue_term;bidx=563242011197570;access-type=ADSL;x-alu-prset-id>\x0d\x0a"
      "P-Nokia-Cookie-IP-Mapping: S1F1=10.0.0.1\x0d\x0a"
      "Max-Forwards: 70\x0d\x0a"
      "Content-Length:  0\x0d\x0a"
      "\x0d\x0a";
  buffer_.add(SIP_NOTIFY_FULL);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(SipDecoderTest, HeaderTest) {
  StateNameValues stateNameValues_;
  EXPECT_EQ("Done", stateNameValues_.name(State::Done));
}

TEST_F(SipDecoderTest, HeaderHandlerTest) { headerHandlerTest(); }

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
