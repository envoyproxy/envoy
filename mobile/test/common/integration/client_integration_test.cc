#include "source/common/quic/quic_server_transport_socket_factory.h"
#include "source/common/quic/server_codec_impl.h"
#include "source/common/tls/cert_validator/default_validator.h"
#include "source/common/tls/server_context_impl.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"
#include "source/extensions/quic/connection_id_generator/envoy_deterministic_connection_id_generator_config.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"
#include "source/extensions/quic/proof_source/envoy_quic_proof_source_factory_impl.h"
#include "source/extensions/udp_packet_writer/default/config.h"

#include "test/common/http/common.h"
#include "test/common/integration/base_client_integration_test.h"
#include "test/common/mocks/common/mocks.h"
#include "test/extensions/filters/http/dynamic_forward_proxy/test_resolver.h"
#include "test/integration/autonomous_upstream.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_random_generator.h"

#include "extension_registry.h"
#include "library/common/bridge/utility.h"
#include "library/common/http/header_utility.h"
#include "library/common/internal_engine.h"
#include "library/common/network/proxy_settings.h"
#include "library/common/types/c_types.h"

using testing::_;
using testing::AnyNumber;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace {

// The only thing this TestKeyValueStore does is return value_ when asked for
// initial loaded contents.
// In this case the TestKeyValueStore will be used for DNS and value will map
// www.lyft.com -> fake test upstream.
class TestKeyValueStore : public Envoy::Platform::KeyValueStore {
public:
  absl::optional<std::string> read(const std::string&) override {
    ASSERT(!value_.empty());
    return value_;
  }
  void save(std::string, std::string) override {}
  void remove(const std::string&) override {}
  void addOrUpdate(absl::string_view, absl::string_view, absl::optional<std::chrono::seconds>) {}
  absl::optional<absl::string_view> get(absl::string_view) { return {}; }
  void flush() {}
  void iterate(::Envoy::KeyValueStore::ConstIterateCb) const {}
  void setValue(std::string value) { value_ = value; }

protected:
  std::string value_;
};

class ClientIntegrationTest
    : public BaseClientIntegrationTest,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, Http::CodecType>> {
public:
  static void SetUpTestCase() { test_key_value_store_ = std::make_shared<TestKeyValueStore>(); }
  static void TearDownTestCase() { test_key_value_store_.reset(); }

  Http::CodecType getCodecType() { return std::get<1>(GetParam()); }

  ClientIntegrationTest() : BaseClientIntegrationTest(/*ip_version=*/std::get<0>(GetParam())) {
    // For server TLS
    Extensions::TransportSockets::Tls::forceRegisterServerContextFactoryImpl();
    // For H3 tests.
    Network::forceRegisterUdpDefaultWriterFactoryFactory();
    Quic::forceRegisterEnvoyQuicCryptoServerStreamFactoryImpl();
    Quic::forceRegisterQuicHttpServerConnectionFactoryImpl();
    Quic::forceRegisterQuicServerTransportSocketConfigFactory();
    Quic::forceRegisterEnvoyQuicProofSourceFactoryImpl();
    Quic::forceRegisterEnvoyDeterministicConnectionIdGeneratorConfigFactory();
    // For H2 tests.
    Extensions::TransportSockets::Tls::forceRegisterDefaultCertValidatorFactory();
  }

  void initialize() override {
    if (getCodecType() == Http::CodecType::HTTP3) {
      setUpstreamProtocol(Http::CodecType::HTTP3);
      builder_.enablePlatformCertificatesValidation(true);
      // Create a k-v store for DNS lookup which createEnvoy() will use to point
      // www.lyft.com -> fake H3 backend.
      add_fake_dns_ = true;
      upstream_tls_ = true;
      add_quic_hints_ = true;
    } else if (getCodecType() == Http::CodecType::HTTP2) {
      setUpstreamProtocol(Http::CodecType::HTTP2);
      builder_.enablePlatformCertificatesValidation(true);
      upstream_tls_ = true;
    }

    if (add_fake_dns_) {
      builder_.addKeyValueStore("reserved.platform_store", test_key_value_store_);
      builder_.enableDnsCache(true, /* save_interval_seconds */ 1);
    }

    BaseClientIntegrationTest::initialize();

    if (getCodecType() == Http::CodecType::HTTP3) {
      auto address = fake_upstreams_[0]->localAddress();
      auto upstream_port = fake_upstreams_[0]->localAddress()->ip()->port();
      default_request_headers_.setHost(fmt::format("www.lyft.com:{}", upstream_port));
      default_request_headers_.setScheme("https");
    } else if (getCodecType() == Http::CodecType::HTTP2) {
      default_request_headers_.setScheme("https");
    }
    builder_.addRuntimeGuard("dns_cache_set_ip_version_to_remove", true);
  }

  void SetUp() override {
    setUpstreamCount(config_helper_.bootstrap().static_resources().clusters_size());
    helper_handle_ = test::SystemHelperPeer::replaceSystemHelper();
    EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _)).Times(AnyNumber());
    EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation())
        .Times(AnyNumber());
  }

  void createEnvoy() override {
    // Allow last minute addition of QUIC hints. This is done lazily as it must be done after
    // upstreams are created.
    auto upstream_port = fake_upstreams_[0]->localAddress()->ip()->port();
    if (add_quic_hints_) {
      // With canonical suffix, having a quic hint of foo.lyft.com will make
      // www.lyft.com being recognized as QUIC ready.
      builder_.addQuicCanonicalSuffix(".lyft.com");
      builder_.addQuicHint("foo.lyft.com", upstream_port);
    }
    if (add_fake_dns_) {
      // Force www.lyft.com to resolve to the fake upstream. It's the only domain
      // name the certs work for so we want that in the request, but we need to
      // fake resolution to not result in a request to the real www.lyft.com
      std::string host = fmt::format("www.lyft.com:{}", upstream_port);
      std::string cache_file_value_contents =
          absl::StrCat(Network::Test::getLoopbackAddressUrlString(version_), ":",
                       fake_upstreams_[0]->localAddress()->ip()->port(), "|1000000|0");
      test_key_value_store_->setValue(absl::StrCat(host.length(), "\n", host,
                                                   cache_file_value_contents.length(), "\n",
                                                   cache_file_value_contents));
    }
    BaseClientIntegrationTest::createEnvoy();
  }

  void TearDown() override {
    if (upstream_connection_) {
      ASSERT_TRUE(upstream_connection_->close());
      ASSERT_TRUE(upstream_connection_->waitForDisconnect());
      upstream_connection_.reset();
    }
    BaseClientIntegrationTest::TearDown();
  }

  void basicTest();
  void trickleTest(bool final_chunk_has_data);
  void explicitFlowControlWithCancels(uint32_t body_size = 1000, bool terminate_engine = false);

  static std::string protocolToString(Http::CodecType type) {
    if (type == Http::CodecType::HTTP3) {
      return "Http3Upstream";
    }
    if (type == Http::CodecType::HTTP2) {
      return "Http2Upstream";
    }
    return "Http1Upstream";
  }

  static std::string testParamsToString(
      const testing::TestParamInfo<std::tuple<Network::Address::IpVersion, Http::CodecType>>
          params) {
    return fmt::format(
        "{}_{}",
        TestUtility::ipTestParamsToString(testing::TestParamInfo<Network::Address::IpVersion>(
            std::get<0>(params.param), params.index)),
        protocolToString(std::get<1>(params.param)));
  }

protected:
  std::unique_ptr<test::SystemHelperPeer::Handle> helper_handle_;
  bool add_quic_hints_ = false;
  bool add_fake_dns_ = false;
  static std::shared_ptr<TestKeyValueStore> test_key_value_store_;
  FakeHttpConnectionPtr upstream_connection_;
  FakeStreamPtr upstream_request_;
};

std::shared_ptr<TestKeyValueStore> ClientIntegrationTest::test_key_value_store_{};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, ClientIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn({Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                                        Http::CodecType::HTTP3})),
    ClientIntegrationTest::testParamsToString);

void ClientIntegrationTest::basicTest() {
  if (getCodecType() != Http::CodecType::HTTP1) {
    EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_)).Times(0);
    EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));
    EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());
  }
  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  default_request_headers_.addCopy(AutonomousStream::EXPECT_REQUEST_SIZE_BYTES,
                                   std::to_string(request_data.length()));

  EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
  stream_callbacks.on_data_ = [this](const Buffer::Instance&, uint64_t, bool, envoy_stream_intel) {
    cc_.on_data_calls_++;
  };

  stream_ = createNewStream(std::move(stream_callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       false);

  stream_->sendData(std::make_unique<Buffer::OwnedImpl>(std::move(request_data)));

  stream_->close(Http::Utility::createRequestTrailerMapPtr());

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_GE(cc_.on_data_calls_, 1);
  ASSERT_EQ(cc_.on_complete_calls_, 1);
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_EQ(cc_.on_header_consumed_bytes_from_response_, 27);
    // HTTP/1
    ASSERT_EQ(1, last_stream_final_intel_.upstream_protocol);
  } else if (upstreamProtocol() == Http::CodecType::HTTP2) {
    ASSERT_EQ(2, last_stream_final_intel_.upstream_protocol);
  } else {
    // This verifies the H3 attempt was made due to the quic hints.
    absl::MutexLock l(&engine_lock_);
    std::string stats = engine_->dumpStats();
    EXPECT_TRUE((absl::StrContains(stats, "cluster.base.upstream_cx_http3_total: 1"))) << stats;
    // Make sure the client reported protocol was also HTTP/3.
    ASSERT_EQ(3, last_stream_final_intel_.upstream_protocol);
  }
}

TEST_P(ClientIntegrationTest, Basic) {
  initialize();
  basicTest();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_EQ(cc_.on_complete_received_byte_count_, 67);
  }
}

#if not defined(__APPLE__)
TEST_P(ClientIntegrationTest, BasicWithCares) {
  builder_.setUseCares(true);
  initialize();
  basicTest();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_EQ(cc_.on_complete_received_byte_count_, 67);
  }
}
#endif

TEST_P(ClientIntegrationTest, LargeResponse) {
  initialize();
  std::string data(1024 * 32, 'a');
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())->setResponseBody(data);
  basicTest();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_EQ(cc_.on_complete_received_byte_count_, 32828);
  } else {
    ASSERT_GE(cc_.on_complete_received_byte_count_, 32000);
  }
}

void ClientIntegrationTest::trickleTest(bool final_chunk_has_data) {
  autonomous_upstream_ = false;

  initialize();

  EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
  stream_callbacks.on_data_ = [this,
                               final_chunk_has_data](const Buffer::Instance&, uint64_t /* length */,
                                                     bool /* end_stream */, envoy_stream_intel) {
    if (explicit_flow_control_) {
      // Allow reading up to 100 bytes.
      stream_->readData(100);
    }
    cc_.on_data_calls_++;
    if (cc_.on_data_calls_ < 10) {
      upstream_request_->encodeData(1, cc_.on_data_calls_ == 9 && final_chunk_has_data);
    }
    if (cc_.on_data_calls_ == 10 && !final_chunk_has_data) {
      upstream_request_->encodeData(0, true);
    }
  };

  stream_ = createNewStream(std::move(stream_callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       false);
  if (explicit_flow_control_) {
    // Allow reading up to 100 bytes
    stream_->readData(100);
  }
  stream_->sendData(std::make_unique<Buffer::OwnedImpl>("request body"));
  stream_->close(Http::Utility::createRequestTrailerMapPtr());

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*BaseIntegrationTest::dispatcher_,
                                                        upstream_connection_));
  ASSERT_TRUE(
      upstream_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*BaseIntegrationTest::dispatcher_));

  upstream_request_->encode1xxHeaders(Http::TestResponseHeaderMapImpl{{":status", "100"}});
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  // This will be read immediately. on_data_ will kick off more chunks.
  upstream_request_->encodeData(1, false);

  terminal_callback_.waitReady();
}

TEST_P(ClientIntegrationTest, Trickle) {
  trickleTest(true);
  ASSERT_LE(cc_.on_data_calls_, 11);
  ASSERT_EQ(cc_.on_error_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 1);
}

TEST_P(ClientIntegrationTest, TrickleExplicitFlowControl) {
  explicit_flow_control_ = true;
  trickleTest(true);
  ASSERT_LE(cc_.on_data_calls_, 11);
  ASSERT_EQ(cc_.on_error_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 1);
}

TEST_P(ClientIntegrationTest, TrickleFinalChunkEmpty) {
  trickleTest(false);
  ASSERT_EQ(cc_.on_data_calls_, 11);
  ASSERT_EQ(cc_.on_error_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 1);
}

TEST_P(ClientIntegrationTest, TrickleExplicitFlowControlFinalChunkEmpty) {
  explicit_flow_control_ = true;
  trickleTest(false);
  ASSERT_EQ(cc_.on_data_calls_, 11);
  ASSERT_EQ(cc_.on_error_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 1);
}

TEST_P(ClientIntegrationTest, ExplicitFlowControlEmptyChunkThenReadData) {
  expect_data_streams_ = false; // Don't validate intel.
  explicit_flow_control_ = true;
  autonomous_upstream_ = false;
  initialize();

  EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();

  stream_ = createNewStream(std::move(stream_callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       false);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*BaseIntegrationTest::dispatcher_,
                                                        upstream_connection_));
  ASSERT_TRUE(
      upstream_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, upstream_request_));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(0, true);

  // Wait for the chunk to hopefully arrive.
  sleep(1);
  stream_->readData(100);

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_data_calls_, 1);
  ASSERT_EQ(cc_.on_error_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 1);
}

TEST_P(ClientIntegrationTest, ManyStreamExplicitFlowControl) {
  explicit_flow_control_ = true;
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESPONSE_SIZE_BYTES, std::to_string(1000));

  uint32_t num_requests = 100;
  std::vector<Platform::StreamPrototypeSharedPtr> prototype_streams;
  std::vector<Platform::StreamSharedPtr> streams;

  for (uint32_t i = 0; i < num_requests; ++i) {
    Platform::StreamPrototypeSharedPtr stream_prototype;
    {
      absl::MutexLock l(&engine_lock_);
      stream_prototype = engine_->streamClient()->newStreamPrototype();
    }

    EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
    stream_callbacks.on_complete_ = [this, &num_requests](envoy_stream_intel,
                                                          envoy_final_stream_intel) {
      cc_.on_complete_calls_++;
      if (cc_.on_complete_calls_ == num_requests) {
        cc_.terminal_callback_->setReady();
      }
    };

    stream_callbacks.on_data_ = [&streams, i](const Buffer::Instance&, uint64_t /* length */,
                                              bool /* end_stream */, envoy_stream_intel) {
      // Allow reading up to 100 bytes.
      streams[i]->readData(100);
    };
    auto stream = createNewStream(std::move(stream_callbacks));
    prototype_streams.push_back(stream_prototype);
    streams.push_back(stream);

    stream->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                        true);
    stream->readData(100);
  }
  ASSERT(streams.size() == num_requests);
  ASSERT(prototype_streams.size() == num_requests);

  terminal_callback_.waitReady();
  ASSERT_EQ(num_requests, cc_.on_complete_calls_);
}

void ClientIntegrationTest::explicitFlowControlWithCancels(const uint32_t body_size,
                                                           const bool terminate_engine) {
  default_request_headers_.addCopy(AutonomousStream::RESPONSE_SIZE_BYTES,
                                   std::to_string(body_size));

  uint32_t num_requests = 100;
  std::vector<Platform::StreamPrototypeSharedPtr> prototype_streams;
  std::vector<Platform::StreamSharedPtr> streams;

  // Randomly select which request number to terminate the engine on.
  uint32_t request_for_engine_termination = 0;
  if (terminate_engine) {
    TestRandomGenerator rand;
    request_for_engine_termination = rand.random() % (num_requests / 2);
  }

  for (uint32_t i = 0; i < num_requests; ++i) {
    Platform::StreamPrototypeSharedPtr stream_prototype;
    {
      absl::MutexLock l(&engine_lock_);
      stream_prototype = engine_->streamClient()->newStreamPrototype();
    }

    EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
    stream_callbacks.on_complete_ = [this, &num_requests](envoy_stream_intel,
                                                          envoy_final_stream_intel) {
      cc_.on_complete_calls_++;
      if (cc_.on_complete_calls_ + cc_.on_cancel_calls_ == num_requests) {
        cc_.terminal_callback_->setReady();
      }
    };
    stream_callbacks.on_cancel_ = [this, &num_requests](envoy_stream_intel,
                                                        envoy_final_stream_intel) {
      cc_.on_cancel_calls_++;
      if (cc_.on_complete_calls_ + cc_.on_cancel_calls_ == num_requests) {
        cc_.terminal_callback_->setReady();
      }
    };
    stream_callbacks.on_data_ = [&streams, i](const Buffer::Instance&, uint64_t /* length */,
                                              bool /* end_stream */, envoy_stream_intel) {
      // Allow reading up to 100 bytes.
      streams[i]->readData(100);
    };
    stream_callbacks.on_error_ = [](const EnvoyError&, envoy_stream_intel,
                                    envoy_final_stream_intel) { RELEASE_ASSERT(0, "unexpected"); };

    auto stream = createNewStream(std::move(stream_callbacks));
    stream->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                        true);
    prototype_streams.push_back(stream_prototype);
    streams.push_back(stream);
    if (i % 2 == 0) {
      stream->cancel();
    } else {
      stream->readData(100);
    }

    if (terminate_engine && request_for_engine_termination == i) {
      absl::MutexLock l(&engine_lock_);
      ASSERT_EQ(engine_->terminate(), ENVOY_SUCCESS);
      engine_.reset();
      break;
    }
  }

  if (terminate_engine) {
    // Only the cancel calls are guaranteed to have completed when engine->terminate() is called.
    EXPECT_GE(cc_.on_cancel_calls_, request_for_engine_termination / 2);
  } else {
    ASSERT(streams.size() == num_requests);
    ASSERT(prototype_streams.size() == num_requests);
    terminal_callback_.waitReady();
    EXPECT_EQ(num_requests / 2, cc_.on_complete_calls_);
    EXPECT_EQ(num_requests / 2, cc_.on_cancel_calls_);
  }
}

TEST_P(ClientIntegrationTest, ManyStreamExplicitFlowWithCancels) {
  explicit_flow_control_ = true;
  initialize();
  explicitFlowControlWithCancels();
}

TEST_P(ClientIntegrationTest, ManyStreamExplicitFlowWithCancelsAfterComplete) {
  explicit_flow_control_ = true;
  initialize();
  explicitFlowControlWithCancels(/*body_size=*/100);
}

TEST_P(ClientIntegrationTest, ManyStreamExplicitFlowWithCancelsAfterCompleteEngineTermination) {
  explicit_flow_control_ = true;
  initialize();
  explicitFlowControlWithCancels(/*body_size=*/100, /*terminate_engine=*/true);
}

TEST_P(ClientIntegrationTest, ClearTextNotPermitted) {
  if (getCodecType() != Http::CodecType::HTTP1) {
    return;
  }
  EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_)).WillRepeatedly(Return(false));

  expect_data_streams_ = false;
  initialize();

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  default_request_headers_.addCopy(AutonomousStream::EXPECT_REQUEST_SIZE_BYTES,
                                   std::to_string(request_data.length()));

  EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
  stream_callbacks.on_data_ = [this](const Buffer::Instance& buffer, uint64_t length,
                                     bool end_stream, envoy_stream_intel) {
    if (end_stream) {
      std::string response_body(length, ' ');
      buffer.copyOut(0, length, response_body.data());
      EXPECT_EQ(response_body, "Cleartext is not permitted");
    }
    cc_.on_data_calls_++;
  };

  stream_ = createNewStream(std::move(stream_callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "400");
  ASSERT_EQ(cc_.on_data_calls_, 1);
  ASSERT_EQ(cc_.on_complete_calls_, 1);
}

TEST_P(ClientIntegrationTest, BasicHttps) {
  EXPECT_CALL(helper_handle_->mock_helper(), isCleartextPermitted(_)).Times(0);
  EXPECT_CALL(helper_handle_->mock_helper(), validateCertificateChain(_, _));
  EXPECT_CALL(helper_handle_->mock_helper(), cleanupAfterCertificateValidation());

  builder_.enablePlatformCertificatesValidation(true);

  upstream_tls_ = true;

  initialize();
  default_request_headers_.setScheme("https");

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  default_request_headers_.addCopy(AutonomousStream::EXPECT_REQUEST_SIZE_BYTES,
                                   std::to_string(request_data.length()));

  EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
  stream_callbacks.on_data_ = [this](const Buffer::Instance& buffer, uint64_t length,
                                     bool end_stream, envoy_stream_intel) {
    if (end_stream) {
      std::string response_body(length, ' ');
      buffer.copyOut(0, length, response_body.data());
      EXPECT_EQ(response_body, "");
    } else {
      EXPECT_EQ(length, 10);
    }
    cc_.on_data_calls_++;
  };

  stream_ = createNewStream(std::move(stream_callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       false);

  stream_->sendData(std::make_unique<Buffer::OwnedImpl>(std::move(request_data)));

  stream_->close(Http::Utility::createRequestTrailerMapPtr());

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_GE(cc_.on_data_calls_, 1);
  ASSERT_EQ(cc_.on_complete_calls_, 1);
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_EQ(cc_.on_complete_received_byte_count_, 67);
  }
}

TEST_P(ClientIntegrationTest, BasicNon2xx) {
  initialize();

  // Set response header status to be non-2xx to test that the correct stats get charged.
  reinterpret_cast<AutonomousUpstream*>(fake_upstreams_.front().get())
      ->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
          Http::TestResponseHeaderMapImpl({{":status", "503"}})));

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls_, 0);
  ASSERT_EQ(cc_.status_, "503");
  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.on_complete_calls_, 1);
}

TEST_P(ClientIntegrationTest, InvalidDomain) {
  initialize();

  default_request_headers_.setHost("www.doesnotexist.com");
  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls_, 1);
  ASSERT_EQ(cc_.on_headers_calls_, 0);
}

TEST_P(ClientIntegrationTest, InvalidDomainFakeResolver) {
  upstream_tls_ = false; // Avoid cert verify errors.
  Network::OverrideAddrInfoDnsResolverFactory factory;
  Registry::InjectFactory<Network::DnsResolverFactory> inject_factory(factory);
  Registry::InjectFactory<Network::DnsResolverFactory>::forceAllowDuplicates();

  initialize();
  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    return;
  }

  default_request_headers_.setHost(
      absl::StrCat("www.doesnotexist.com:", fake_upstreams_[0]->localAddress()->ip()->port()));
  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  // Force the lookup to resolve to localhost.
  Network::TestResolver::unblockResolve("localhost");
  terminal_callback_.waitReady();

  // Instead of an error we should get a 200 ok from the fake upstream.
  ASSERT_EQ(cc_.on_error_calls_, 0);
  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "200");

  // Kick off a second request. There should be no resolution.
  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();
  EXPECT_EQ(1, getCounterValue("dns_cache.base_dns_cache.dns_query_attempt"));
}

TEST_P(ClientIntegrationTest, InvalidDomainReresolveWithNoAddresses) {
  builder_.addRuntimeGuard("reresolve_null_addresses", true);
  Network::OverrideAddrInfoDnsResolverFactory factory;
  Registry::InjectFactory<Network::DnsResolverFactory> inject_factory(factory);
  Registry::InjectFactory<Network::DnsResolverFactory>::forceAllowDuplicates();

  initialize();
  default_request_headers_.setHost(
      absl::StrCat("www.doesnotexist.com:", fake_upstreams_[0]->localAddress()->ip()->port()));
  stream_ = stream_prototype_->start(createDefaultStreamCallbacks(), explicit_flow_control_);
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  // Unblock resolve, but resolve to the bad domain.
  ASSERT_TRUE(waitForCounterGe("dns_cache.base_dns_cache.dns_query_attempt", 1));
  Network::TestResolver::unblockResolve();
  terminal_callback_.waitReady();

  // The stream should fail.
  ASSERT_EQ(cc_.on_error_calls_, 1);

  // A new stream will kick off a new resolution because the address was null.
  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  Network::TestResolver::unblockResolve();
  terminal_callback_.waitReady();
  EXPECT_LE(2, getCounterValue("dns_cache.base_dns_cache.dns_query_attempt"));
}

TEST_P(ClientIntegrationTest, ReresolveAndDrain) {
  builder_.enableDrainPostDnsRefresh(true);
  add_fake_dns_ = true;
  Network::OverrideAddrInfoDnsResolverFactory factory;
  Registry::InjectFactory<Network::DnsResolverFactory> inject_factory(factory);
  Registry::InjectFactory<Network::DnsResolverFactory>::forceAllowDuplicates();

  initialize();
  if (version_ != Network::Address::IpVersion::v4) {
    return; // This test relies on ipv4 loopback.
  }

  auto next_address = Network::Utility::parseInternetAddressNoThrow(
      "127.0.0.3", fake_upstreams_[0]->localAddress()->ip()->port());
  // This will hopefully be miniminally flaky because of low use of 127.0.0.3
  // but may need to be disabled.
  createUpstream(next_address, upstreamConfig());
  ASSERT_EQ(fake_upstreams_.size(), 3);

  // Make the "original" upstream and reresolve upstream return different errors.
  auto* au = reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[0].get());
  au->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "201"}})));
  au = reinterpret_cast<AutonomousUpstream*>(fake_upstreams_[2].get());
  au->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(
      Http::TestResponseHeaderMapImpl({{":status", "202"}})));

  // Send a request. The original upstream should be used because of DNS block.
  default_request_headers_.setHost(
      absl::StrCat("www.lyft.com:", fake_upstreams_[0]->localAddress()->ip()->port()));
  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.status_, "201");
  // No DNS queries, because of the initial cache load.
  EXPECT_EQ(0, getCounterValue("dns_cache.base_dns_cache.dns_query_attempt"));
  EXPECT_EQ(1, getCounterValue("dns_cache.base_dns_cache.cache_load"));

  // Reset connectivity state. This should force a resolve but we will not
  // unblock it.
  {
    absl::MutexLock l(&engine_lock_);
    engine_->engine()->resetConnectivityState();
  }

  // Make sure the attempt happened.
  ASSERT_TRUE(waitForCounterGe("dns_cache.base_dns_cache.dns_query_attempt", 1));
  EXPECT_EQ(0, getCounterValue("dns_cache.base_dns_cache.dns_query_success"));
  // The next request should go to the original upstream as there's been no drain.
  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.status_, "201");
  // No DNS query should have finished.
  EXPECT_EQ(0, getCounterValue("dns_cache.base_dns_cache.dns_query_success"));

  // Force the lookup to resolve to localhost.
  // Unblock the resolution and wait for it to succeed.
  Network::TestResolver::unblockResolve("127.0.0.3");
  ASSERT_TRUE(waitForCounterGe("dns_cache.base_dns_cache.dns_query_success", 1));

  // Do one final request. It should go to the second upstream and return 202
  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.status_, "202");
}

TEST_P(ClientIntegrationTest, BasicBeforeResponseHeaders) {
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESET_AFTER_REQUEST, "yes");

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls_, 1);
  ASSERT_EQ(cc_.on_headers_calls_, 0);
}

TEST_P(ClientIntegrationTest, ExplicitBeforeResponseHeaders) {
  explicit_flow_control_ = true;
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESET_AFTER_REQUEST, "yes");

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls_, 1);
  ASSERT_EQ(cc_.on_headers_calls_, 0);
}

TEST_P(ClientIntegrationTest, ResetAfterResponseHeaders) {
  autonomous_allow_incomplete_streams_ = true;
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESET_AFTER_RESPONSE_HEADERS, "yes");
  default_request_headers_.addCopy(AutonomousStream::RESPONSE_DATA_BLOCKS, "1");

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls_, 1);
}

TEST_P(ClientIntegrationTest, ResetAfterResponseHeadersExplicit) {
  explicit_flow_control_ = true;
  autonomous_allow_incomplete_streams_ = true;
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESET_AFTER_RESPONSE_HEADERS, "yes");
  default_request_headers_.addCopy(AutonomousStream::RESPONSE_DATA_BLOCKS, "1");

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  // Read the body chunk. This releases the error.
  stream_->readData(100);

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls_, 1);
}

TEST_P(ClientIntegrationTest, ResetAfterHeaderOnlyResponse) {
  autonomous_allow_incomplete_streams_ = true;
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESET_AFTER_RESPONSE_HEADERS, "yes");
  default_request_headers_.addCopy(AutonomousStream::RESPONSE_DATA_BLOCKS, "0");

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       false);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls_, 1);
}

TEST_P(ClientIntegrationTest, ResetBetweenDataChunks) {
  autonomous_allow_incomplete_streams_ = true;
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESET_AFTER_RESPONSE_DATA, "yes");
  default_request_headers_.addCopy(AutonomousStream::RESPONSE_DATA_BLOCKS, "2");

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_error_calls_, 1);
}

TEST_P(ClientIntegrationTest, ResetAfterDataExplicit) {
  explicit_flow_control_ = true;

  autonomous_allow_incomplete_streams_ = true;
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESET_AFTER_RESPONSE_DATA, "yes");
  default_request_headers_.addCopy(AutonomousStream::RESPONSE_DATA_BLOCKS, "1");

  auto callbacks = createDefaultStreamCallbacks();
  stream_ = createNewStream(std::move(callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);

  // Allow passing up the data and error
  stream_->readData(100);
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_data_calls_, 1);
  ASSERT_EQ(cc_.on_error_calls_, 1);
}

TEST_P(ClientIntegrationTest, ResetAfterDataExplicitMultipleChunks) {
  explicit_flow_control_ = true;

  autonomous_allow_incomplete_streams_ = true;
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESET_AFTER_RESPONSE_DATA, "yes");
  default_request_headers_.addCopy(AutonomousStream::RESPONSE_DATA_BLOCKS, "1");

  auto callbacks = createDefaultStreamCallbacks();
  ConditionalInitializer initial_data;
  callbacks.on_data_ = [this, &initial_data](const Buffer::Instance&, uint64_t /* length */,
                                             bool /* end_stream */, envoy_stream_intel) {
    if (!cc_.on_data_calls_) {
      initial_data.setReady();
    }
    cc_.on_data_calls_++;
  };

  stream_ = createNewStream(std::move(callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);

  // Default body size is 10 - this will force 2 reads.
  stream_->readData(5);
  initial_data.waitReady();
  stream_->readData(5);
  terminal_callback_.waitReady();

  // Make sure we get both chunks before flushing the error.
  ASSERT_EQ(cc_.on_data_calls_, 2);
  ASSERT_EQ(cc_.on_error_calls_, 1);
}

TEST_P(ClientIntegrationTest, CancelBeforeRequestHeadersSent) {
  autonomous_upstream_ = false;
  initialize();

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->cancel();

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_cancel_calls_, 1);
}

TEST_P(ClientIntegrationTest, CancelAfterRequestHeadersSent) {
  initialize();

  default_request_headers_.addCopy(AutonomousStream::RESPOND_AFTER_REQUEST_HEADERS, "yes");

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       false);
  stream_->cancel();
  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.on_cancel_calls_, 1);
}

TEST_P(ClientIntegrationTest, CancelAfterRequestComplete) {
  autonomous_upstream_ = false;
  initialize();

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  stream_->cancel();
  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.on_cancel_calls_, 1);
}

TEST_P(ClientIntegrationTest, CancelDuringResponse) {
  autonomous_upstream_ = false;
  initialize();
  ConditionalInitializer headers_callback;

  EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
  stream_callbacks.on_headers_ = [this, &headers_callback](const Http::ResponseHeaderMap& headers,
                                                           bool /* end_stream */,
                                                           envoy_stream_intel) {
    cc_.status_ = headers.getStatusValue();
    cc_.on_headers_calls_++;
    headers_callback.setReady();
    return nullptr;
  };

  stream_ = createNewStream(std::move(stream_callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*BaseIntegrationTest::dispatcher_,
                                                        upstream_connection_));
  ASSERT_TRUE(
      upstream_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, upstream_request_));
  // Send an incomplete response.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  headers_callback.waitReady();
  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_EQ(cc_.on_data_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 0);

  // Now cancel, and make sure the cancel is received.
  stream_->cancel();
  memset(&cc_.final_intel_, 0, sizeof(cc_.final_intel_));
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_EQ(cc_.on_data_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 0);
  ASSERT_EQ(cc_.on_cancel_calls_, 1);

  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }

  // Close the HTTP3 connection and verify stats are dumped properly.
  if (getCodecType() == Http::CodecType::HTTP3) {
    ASSERT_TRUE(upstream_connection_->close());
    ASSERT_TRUE(upstream_connection_->waitForDisconnect());
    upstream_connection_.reset();
    ASSERT_TRUE(
        waitForCounterGe("http3.upstream.tx.quic_connection_close_error_code_QUIC_NO_ERROR", 1));
    ASSERT_TRUE(waitForCounterGe(
        "http3.upstream.tx.quic_reset_stream_error_code_QUIC_STREAM_REQUEST_REJECTED", 1));
  }
}

TEST_P(ClientIntegrationTest, BasicCancelWithCompleteStream) {
  autonomous_upstream_ = false;

  initialize();

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*BaseIntegrationTest::dispatcher_,
                                                        upstream_connection_));
  ASSERT_TRUE(
      upstream_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, upstream_request_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);

  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_EQ(cc_.on_complete_calls_, 1);

  // Now cancel. As on_complete has been called cancel is a no-op but is
  // non-problematic.
  stream_->cancel();
}

TEST_P(ClientIntegrationTest, CancelWithPartialStream) {
  autonomous_upstream_ = false;
  explicit_flow_control_ = true;
  initialize();
  ConditionalInitializer headers_callback;

  EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
  stream_callbacks.on_headers_ = [this, &headers_callback](const Http::ResponseHeaderMap& headers,
                                                           bool /* end_stream */,
                                                           envoy_stream_intel) {
    cc_.status_ = headers.getStatusValue();
    cc_.on_headers_calls_++;
    headers_callback.setReady();
    return nullptr;
  };

  stream_ = createNewStream(std::move(stream_callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*BaseIntegrationTest::dispatcher_,
                                                        upstream_connection_));
  ASSERT_TRUE(
      upstream_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, upstream_request_));

  // Send a complete response with body.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1, true);

  headers_callback.waitReady();
  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_EQ(cc_.on_data_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 0);

  // Due to explicit flow control, the upstream stream is complete, but the
  // callbacks will not be called for data and completion. Cancel the stream
  // and make sure the cancel is received.
  stream_->cancel();
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_EQ(cc_.on_data_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 0);
  ASSERT_EQ(cc_.on_cancel_calls_, 1);
}

// Test header key case sensitivity.
TEST_P(ClientIntegrationTest, CaseSensitive) {
  if (getCodecType() != Http::CodecType::HTTP1) {
    return;
  }
  autonomous_upstream_ = false;
  initialize();

  auto headers = Http::Utility::createRequestHeaderMapPtr();
  HttpTestUtility::addDefaultHeaders(*headers);
  headers->setHost(fake_upstreams_[0]->localAddress()->asStringView());
  Http::StatefulHeaderKeyFormatter& formatter = headers->formatter().value();
  formatter.processKey("FoO");
  headers->addCopy(Http::LowerCaseString("FoO"), "bar");

  EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
  stream_callbacks.on_headers_ = [this](const Http::ResponseHeaderMap& headers, bool,
                                        envoy_stream_intel) {
    cc_.status_ = headers.getStatusValue();
    cc_.on_headers_calls_++;
    auto result = headers.get(Http::LowerCaseString("My-ResponsE-Header"));
    ASSERT_FALSE(result.empty());
    EXPECT_TRUE(result[0]->value() == "foo");
  };

  stream_ = createNewStream(std::move(stream_callbacks));
  stream_->sendHeaders(std::move(headers), true);

  Envoy::FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));

  // Verify that the upstream request has preserved cased headers.
  std::string upstream_request;
  EXPECT_TRUE(upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("GET /"),
                                               &upstream_request));
  EXPECT_TRUE(absl::StrContains(upstream_request, "FoO: bar")) << upstream_request;

  // Send mixed case headers, and verify via setOnHeaders they are received correctly.
  auto response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nMy-ResponsE-Header: foo\r\n\r\n";
  ASSERT_TRUE(upstream_connection->write(response));

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_EQ(cc_.on_data_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 1);
}

TEST_P(ClientIntegrationTest, TimeoutOnRequestPath) {
  builder_.setStreamIdleTimeoutSeconds(1);

  autonomous_upstream_ = false;
  initialize();

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       false);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*BaseIntegrationTest::dispatcher_,
                                                        upstream_connection_));
  ASSERT_TRUE(
      upstream_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, upstream_request_));

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls_, 0);
  ASSERT_EQ(cc_.on_data_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 0);
  ASSERT_EQ(cc_.on_error_calls_, 1);

  if (getCodecType() != Http::CodecType::HTTP1) {
    ASSERT_TRUE(upstream_request_->waitForReset());
  } else {
    ASSERT_TRUE(upstream_connection_->waitForDisconnect());
  }
}

TEST_P(ClientIntegrationTest, TimeoutOnResponsePath) {
  builder_.setStreamIdleTimeoutSeconds(1);
  autonomous_upstream_ = false;
  initialize();

  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*BaseIntegrationTest::dispatcher_,
                                                        upstream_connection_));
  ASSERT_TRUE(
      upstream_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, upstream_request_));

  // Send response headers but no body.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);

  // Wait for timeout.
  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls_, 1);
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_EQ(cc_.on_data_calls_, 0);
  ASSERT_EQ(cc_.on_complete_calls_, 0);
  ASSERT_EQ(cc_.on_error_calls_, 1);

  if (upstreamProtocol() != Http::CodecType::HTTP1) {
    ASSERT_TRUE(upstream_request_->waitForReset());
  }
}

TEST_P(ClientIntegrationTest, ResetWithBidiTraffic) {
  autonomous_upstream_ = false;
  initialize();
  ConditionalInitializer headers_callback;

  EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
  stream_callbacks.on_headers_ = [this, &headers_callback](const Http::ResponseHeaderMap& headers,
                                                           bool /* end_stream */,
                                                           envoy_stream_intel) {
    cc_.status_ = headers.getStatusValue();
    cc_.on_headers_calls_++;
    headers_callback.setReady();
    return nullptr;
  };

  stream_ = createNewStream(std::move(stream_callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       false);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*BaseIntegrationTest::dispatcher_,
                                                        upstream_connection_));
  ASSERT_TRUE(
      upstream_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, upstream_request_));

  // Send response headers but no body.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  // Make sure the headers are sent up.
  headers_callback.waitReady();
  // Reset the stream.
  upstream_request_->encodeResetStream();

  // Encoding data should not be problematic.
  stream_->sendData(std::make_unique<Buffer::OwnedImpl>("request body"));
  // Make sure cancel isn't problematic.
  stream_->cancel();
}

TEST_P(ClientIntegrationTest, ResetWithBidiTrafficExplicitData) {
  explicit_flow_control_ = true;
  autonomous_upstream_ = false;
  builder_.setLogLevel(Logger::Logger::debug);
  initialize();
  ConditionalInitializer headers_callback;

  EnvoyStreamCallbacks stream_callbacks = createDefaultStreamCallbacks();
  stream_callbacks.on_headers_ = [this, &headers_callback](const Http::ResponseHeaderMap& headers,
                                                           bool /* end_stream */,
                                                           envoy_stream_intel) {
    cc_.status_ = headers.getStatusValue();
    cc_.on_headers_calls_++;
    headers_callback.setReady();
    return nullptr;
  };

  stream_ = createNewStream(std::move(stream_callbacks));
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       false);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*BaseIntegrationTest::dispatcher_,
                                                        upstream_connection_));
  ASSERT_TRUE(
      upstream_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, upstream_request_));

  // Send response headers and body but no fin.
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(1, false);
  upstream_request_->encodeResetStream();
  // Make sure the headers are sent up.
  headers_callback.waitReady();

  // Encoding data should not be problematic.
  stream_->sendData(std::make_unique<Buffer::OwnedImpl>("request body"));
  // Make sure cancel isn't problematic.
  stream_->cancel();
}

TEST_P(ClientIntegrationTest, Proxying) {
  if (getCodecType() != Http::CodecType::HTTP1) {
    return;
  }
  initialize();
  {
    absl::MutexLock l(&engine_lock_);
    engine_->engine()->setProxySettings(fake_upstreams_[0]->localAddress()->asString().c_str(),
                                        fake_upstreams_[0]->localAddress()->ip()->port());
  }

  stream_ = createNewStream(createDefaultStreamCallbacks());
  // The initial request will do the DNS lookup.
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_EQ(cc_.on_complete_calls_, 1);
  stream_.reset();

  // The second request will use the cached DNS entry and should succeed as well.
  stream_ = createNewStream(createDefaultStreamCallbacks());
  stream_->sendHeaders(std::make_unique<Http::TestRequestHeaderMapImpl>(default_request_headers_),
                       true);
  terminal_callback_.waitReady();
  ASSERT_EQ(cc_.status_, "200");
  ASSERT_EQ(cc_.on_complete_calls_, 2);
}

TEST_P(ClientIntegrationTest, TestRuntimeSet) {
  builder_.addRuntimeGuard("test_feature_true", false);
  builder_.addRuntimeGuard("test_feature_false", true);
  initialize();

  // Verify that the Runtime config values are from the RTDS response.
  EXPECT_TRUE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
  EXPECT_FALSE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_true"));
}

TEST_P(ClientIntegrationTest, TestStats) {
  initialize();

  {
    absl::MutexLock l(&engine_lock_);
    std::string stats = engine_->dumpStats();
    EXPECT_TRUE((absl::StrContains(stats, "runtime.load_success: 1"))) << stats;
  }
}

#if defined(__APPLE__)
TEST_P(ClientIntegrationTest, TestProxyResolutionApi) {
  builder_.respectSystemProxySettings(true);
  initialize();
  ASSERT_TRUE(Envoy::Api::External::retrieveApi("envoy_proxy_resolver") != nullptr);
}
#endif

// This test is simply to test the IPv6 connectivity check and DNS refresh and make sure the code
// doesn't crash. It doesn't really test the actual network change event.
TEST_P(ClientIntegrationTest, OnNetworkChanged) {
  builder_.addRuntimeGuard("dns_cache_set_ip_version_to_remove", true);
  initialize();
  internalEngine()->onDefaultNetworkChanged(NetworkType::WLAN);
  basicTest();
  if (upstreamProtocol() == Http::CodecType::HTTP1) {
    ASSERT_EQ(cc_.on_complete_received_byte_count_, 67);
  }
}

} // namespace
} // namespace Envoy
