#include <memory>

#include "envoy/stats/stats.h"

#include "source/common/secret/secret_manager_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.h"
#include "contrib/envoy/extensions/filters/http/sxg/v3alpha/sxg.pb.validate.h"
#include "contrib/sxg/filters/http/source/filter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SXG {

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

class MockSecretReader : public SecretReader {
public:
  MockSecretReader(const std::string& certificate, const std::string& private_key)
      : certificate_(certificate), private_key_(private_key){};

  const std::string& certificate() const override { return certificate_; }
  const std::string& privateKey() const override { return private_key_; }

private:
  const std::string certificate_;
  const std::string private_key_;
};

class MockEncoder : public Encoder {
public:
  MOCK_METHOD(void, setOrigin, (const std::string), (override));
  MOCK_METHOD(void, setUrl, (const std::string), (override));
  MOCK_METHOD(bool, loadSigner, (), (override));
  MOCK_METHOD(bool, loadHeaders, (Http::ResponseHeaderMap*), (override));
  MOCK_METHOD(bool, loadContent, (Buffer::Instance&), (override));
  MOCK_METHOD(bool, getEncodedResponse, (), (override));
  MOCK_METHOD(Buffer::BufferFragment*, writeSxg, (), (override));
};

int extractIntFromBytes(std::string bytes, size_t offset, size_t size) {
  if (size <= 0 || size > 8 || bytes.size() < offset + size) {
    return 0;
  }
  int value = 0;
  for (size_t i = 0; i < size; i++) {
    value <<= 8;
    value |= (0xff & bytes[offset + i]);
  }
  return value;
}

bool writeIntToBytes(std::string& bytes, uint64_t int_to_write, size_t offset, size_t size) {
  if (size <= 0 || size > 8 || bytes.size() < offset + size) {
    return false;
  }
  for (int i = size - 1; i >= 0; i--) {
    char byte = 0xff & int_to_write;
    bytes[offset + i] = byte;
    int_to_write >>= 8;
  }
  return true;
}

// The sig value of the SXG document is unique, so we strip it in tests
bool clearSignature(std::string& buffer) {
  if (buffer.find("sxg1-b3", 0, 7) == std::string::npos) {
    return false;
  }
  if (buffer[7] != '\0') {
    return false;
  }

  // The fallback URL length is contained in the 2 bytes following the sxg-b3
  // prefix string and the nullptr byte that follows. We need to know this length
  // because the signature length is located after the fallback URL.
  size_t fallback_url_size_offset = 8;
  size_t fallback_url_size = extractIntFromBytes(buffer, fallback_url_size_offset, 2);

  // the signature length is contained in the 3 bytes following the fallback URL
  size_t sig_size_offset = fallback_url_size_offset + 2 + fallback_url_size;
  size_t sig_size = extractIntFromBytes(buffer, sig_size_offset, 3);

  const size_t sig_pos = buffer.find("sig=*");
  if (sig_pos == std::string::npos) {
    return false;
  }

  const size_t start = sig_pos + 5;
  const size_t len = buffer.find('*', start) - start;

  // decrement the sig_size in the SXG document by the calculated length
  const size_t modified_sig_size = sig_size - len;
  if (!writeIntToBytes(buffer, modified_sig_size, sig_size_offset, 3)) {
    return false;
  }

  // replace the signature piece with empty string
  buffer.erase(start, len);

  return true;
}

class FilterTest : public testing::Test {
public:
  FilterTest() = default;

  void setConfiguration() {
    std::string config_str(R"YAML(
cbor_url: "/.sxg/cert.cbor"
validity_url: "/.sxg/validity.msg"
)YAML");
    setConfiguration(config_str);
  }

  void setConfiguration(const std::string& config_str) {
    std::string certificate(R"PEM(
-----BEGIN CERTIFICATE-----
MIIBhjCCASygAwIBAgIJAIH9REPqIFXTMAkGByqGSM49BAEwMjEUMBIGA1UEAwwL
ZXhhbXBsZS5vcmcxDTALBgNVBAoMBFRlc3QxCzAJBgNVBAYTAlVTMB4XDTIxMDEx
MzAxMDcwMVoXDTIxMDQxMzAxMDcwMVowMjEUMBIGA1UEAwwLZXhhbXBsZS5vcmcx
DTALBgNVBAoMBFRlc3QxCzAJBgNVBAYTAlVTMFkwEwYHKoZIzj0CAQYIKoZIzj0D
AQcDQgAE4ZrHsGLEiP+pV70a8zIERNcu9MBJHHfbeqLUqwGWWU2/YHObf58nE9to
c6lgrko2JdbV6TyWLVUc/M0Pn+OVSaMsMCowEAYKKwYBBAHWeQIBFgQCBQAwFgYD
VR0RBA8wDYILZXhhbXBsZS5vcmcwCQYHKoZIzj0EAQNJADBGAiEAuQJjX+z7j4hR
xtxfs4VPY5RsF5Sawd+mtluRxpoURcsCIQCIGU/11jcuS0UbIpt4B5Gb1UJlSKGi
Dgu+2OKt7qVPrA==
-----END CERTIFICATE-----
)PEM");
    std::string private_key(R"PEM(
-----BEGIN EC PARAMETERS-----
BggqhkjOPQMBBw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIJyGXecxIQtBwBJWU4Sc5A8UHNt5HnOBR9Oh11AGYa/2oAoGCCqGSM49
AwEHoUQDQgAE4ZrHsGLEiP+pV70a8zIERNcu9MBJHHfbeqLUqwGWWU2/YHObf58n
E9toc6lgrko2JdbV6TyWLVUc/M0Pn+OVSQ==
-----END EC PRIVATE KEY-----
)PEM");

    setConfiguration(config_str, certificate, private_key);
  }

  void setConfiguration(const std::string& config_str, const std::string& certificate,
                        const std::string& private_key) {
    envoy::extensions::filters::http::sxg::v3alpha::SXG proto;
    TestUtility::loadFromYaml(config_str, proto);

    time_system_.setSystemTime(std::chrono::seconds(1610503040));

    auto secret_reader = std::make_shared<MockSecretReader>(certificate, private_key);
    config_ = std::make_shared<FilterConfig>(proto, time_system_, secret_reader, "", scope_);
  }

  void setFilter() {
    if (encoder_ == nullptr) {
      encoder_ = std::make_unique<EncoderImpl>(config_);
    }
    setFilter(std::make_shared<Filter>(config_, encoder_));
  }

  void setFilter(std::shared_ptr<Filter> filter) {
    filter_ = filter;
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void testPassthroughHtml(Http::TestRequestHeaderMapImpl& request_headers,
                           Http::TestResponseHeaderMapImpl& response_headers,
                           bool client_can_accept_sxg) {
    testPassthroughHtml(request_headers, response_headers, nullptr, client_can_accept_sxg);
  }

  void testPassthroughHtml(Http::TestRequestHeaderMapImpl& request_headers,
                           Http::TestResponseHeaderMapImpl& response_headers,
                           Http::TestResponseTrailerMapImpl* response_trailers,
                           bool client_can_accept_sxg) {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
    Buffer::OwnedImpl data("<html><body>hi!</body></html>\n");

    auto on_modify_encoding_buffer = [&data](std::function<void(Buffer::Instance&)> cb) {
      cb(data);
    };
    EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer)
        .WillRepeatedly(Invoke(on_modify_encoding_buffer));

    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));
    if (response_trailers) {
      EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(*response_trailers));
    }

    EXPECT_EQ(response_headers.get(Http::LowerCaseString("content-type")).size(), 1);
    EXPECT_EQ(
        response_headers.get(Http::LowerCaseString("content-type"))[0]->value().getStringView(),
        "text/html");
    EXPECT_EQ("<html><body>hi!</body></html>\n", data.toString());

    const Envoy::Http::LowerCaseString x_client_can_accept_sxg_key("x-client-can-accept-sxg");
    if (client_can_accept_sxg) {
      EXPECT_FALSE(request_headers.get(x_client_can_accept_sxg_key).empty());
      EXPECT_EQ("true",
                request_headers.get(x_client_can_accept_sxg_key)[0]->value().getStringView());
      EXPECT_EQ(1UL, store_.counter("sxg.total_client_can_accept_sxg").value());
    } else {
      const Envoy::Http::LowerCaseString x_client_can_accept_sxg_key("x-client-can-accept-sxg");
      EXPECT_TRUE(request_headers.get(x_client_can_accept_sxg_key).empty());
      EXPECT_EQ(0UL, store_.counter("sxg.total_client_can_accept_sxg").value());
    }
    EXPECT_EQ(0UL, store_.counter("sxg.total_should_sign").value());
    EXPECT_EQ(0UL, store_.counter("sxg.total_exceeded_max_payload_size").value());
    EXPECT_EQ(0UL, store_.counter("sxg.total_signed_attempts").value());
    EXPECT_EQ(0UL, store_.counter("sxg.total_signed_succeeded").value());
    EXPECT_EQ(0UL, store_.counter("sxg.total_signed_failed").value());
  }

  void testFallbackToHtml(Http::TestRequestHeaderMapImpl& request_headers,
                          Http::TestResponseHeaderMapImpl& response_headers,
                          bool exceeded_max_payload_size, bool attempted_encode) {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->encodeHeaders(response_headers, false));
    Buffer::OwnedImpl data("<html><body>hi!</body></html>\n");

    auto on_modify_encoding_buffer = [&data](std::function<void(Buffer::Instance&)> cb) {
      cb(data);
    };
    EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer)
        .WillRepeatedly(Invoke(on_modify_encoding_buffer));

    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data, true));
    EXPECT_EQ(response_headers.get(Http::LowerCaseString("content-type")).size(), 1);
    EXPECT_EQ(
        response_headers.get(Http::LowerCaseString("content-type"))[0]->value().getStringView(),
        "text/html");
    EXPECT_EQ("<html><body>hi!</body></html>\n", data.toString());

    const Envoy::Http::LowerCaseString x_client_can_accept_sxg_key("x-client-can-accept-sxg");
    EXPECT_FALSE(request_headers.get(x_client_can_accept_sxg_key).empty());
    EXPECT_EQ("true", request_headers.get(x_client_can_accept_sxg_key)[0]->value().getStringView());
    EXPECT_EQ(1UL, store_.counter("sxg.total_client_can_accept_sxg").value());
    EXPECT_EQ(1UL, store_.counter("sxg.total_should_sign").value());
    EXPECT_EQ(exceeded_max_payload_size ? 1UL : 0UL,
              store_.counter("sxg.total_exceeded_max_payload_size").value());
    EXPECT_EQ(attempted_encode ? 1UL : 0L, store_.counter("sxg.total_signed_attempts").value());
    EXPECT_EQ(0UL, store_.counter("sxg.total_signed_succeeded").value());
    EXPECT_EQ(attempted_encode ? 1UL : 0UL, store_.counter("sxg.total_signed_failed").value());
  }

  void testEncodeSignedExchange(Http::TestRequestHeaderMapImpl& request_headers,
                                Http::TestResponseHeaderMapImpl& response_headers) {
    testEncodeSignedExchange(request_headers, response_headers, nullptr);
  }

  void testEncodeSignedExchange(Http::TestRequestHeaderMapImpl& request_headers,
                                Http::TestResponseHeaderMapImpl& response_headers,
                                Http::TestResponseTrailerMapImpl* response_trailers) {
    const Buffer::OwnedImpl sxg(
        "sxg1-b3\0\0\x1Ehttps://example.org/hello.html\0\x1\0\0\0\x84"
        "label;cert-sha256=*unJ3rwJT2DwWlJAw1lfVLvPjeYoJh0+QUQ97zJQPZtc=*;cert-url=\"https://"
        "example.org/.sxg/"
        "cert.cbor?d=ba7277af0253d83c\";date=1610416640;expires=1611021440;integrity=\"digest/"
        "mi-sha256-03\";sig=**;validity-url=\"https://example.org/.sxg/"
        "validity.msg\"\xA4"
        "FdigestX9mi-sha256-03=0x0E2wkWVYOJ7Gq8+Kfaiyjo3gYCyaijhGGgkzjPoTo=G:statusC200Lcontent-"
        "typeItext/htmlPcontent-encodingLmi-sha256-03\0\0\0\0\0\0\x10\0<html><body>hi!</body></"
        "html>\n",
        472);
    testEncodeSignedExchange(request_headers, response_headers, response_trailers, sxg);
  }

  void testEncodeSignedExchange(Http::TestRequestHeaderMapImpl& request_headers,
                                Http::TestResponseHeaderMapImpl& response_headers,
                                const Buffer::OwnedImpl& sxg) {
    testEncodeSignedExchange(request_headers, response_headers, nullptr, sxg);
  }

  void testEncodeSignedExchange(Http::TestRequestHeaderMapImpl& request_headers,
                                Http::TestResponseHeaderMapImpl& response_headers,
                                Http::TestResponseTrailerMapImpl* response_trailers,
                                const Buffer::OwnedImpl& sxg) {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
    EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
              filter_->encodeHeaders(response_headers, false));

    Buffer::OwnedImpl accumulated_data;

    EXPECT_CALL(encoder_callbacks_, addEncodedData(_, false))
        .Times(2)
        .WillRepeatedly(Invoke(
            [&accumulated_data](Buffer::Instance& data, bool) { accumulated_data.add(data); }));

    auto on_modify_encoding_buffer =
        [&accumulated_data](std::function<void(Buffer::Instance&)> cb) { cb(accumulated_data); };
    EXPECT_CALL(encoder_callbacks_, modifyEncodingBuffer)
        .WillRepeatedly(Invoke(on_modify_encoding_buffer));

    Buffer::OwnedImpl chunk1("<html><body>hi!", 15);
    Buffer::OwnedImpl chunk2("</body></html>\n", 15);
    EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(chunk1, false));
    if (response_trailers) {
      EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->encodeData(chunk2, false));
      EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(*response_trailers));
    } else {
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(chunk2, true));
    }

    std::string result = accumulated_data.toString();
    EXPECT_TRUE(clearSignature(result));
    EXPECT_EQ(response_headers.get(Http::LowerCaseString("content-type")).size(), 1);
    EXPECT_EQ(
        response_headers.get(Http::LowerCaseString("content-type"))[0]->value().getStringView(),
        "application/signed-exchange;v=b3");
    EXPECT_EQ(response_headers.get(Http::LowerCaseString("content-length")).size(), 1);
    EXPECT_EQ(
        response_headers.get(Http::LowerCaseString("content-length"))[0]->value().getStringView(),
        std::to_string(accumulated_data.length()));
    EXPECT_EQ(sxg.toString(), result);

    const Envoy::Http::LowerCaseString x_client_can_accept_sxg_key("x-client-can-accept-sxg");
    EXPECT_FALSE(request_headers.get(x_client_can_accept_sxg_key).empty());
    EXPECT_EQ("true", request_headers.get(x_client_can_accept_sxg_key)[0]->value().getStringView());
    EXPECT_EQ(1UL, store_.counter("sxg.total_client_can_accept_sxg").value());
    EXPECT_EQ(1UL, store_.counter("sxg.total_should_sign").value());
    EXPECT_EQ(0UL, store_.counter("sxg.total_exceeded_max_payload_size").value());
    EXPECT_EQ(1UL, store_.counter("sxg.total_signed_attempts").value());
    EXPECT_EQ(1UL, store_.counter("sxg.total_signed_succeeded").value());
    EXPECT_EQ(0UL, store_.counter("sxg.total_signed_failed").value());
  }

  void callDoSxgAgain() { filter_->doSxg(); }

  Stats::TestUtil::TestStore store_;
  Stats::Scope& scope_{*store_.rootScope()};
  Event::SimulatedTimeSystem time_system_;
  std::shared_ptr<FilterConfig> config_;
  std::unique_ptr<Encoder> encoder_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::shared_ptr<Filter> filter_;
};

// Verifies that the OAuth SDSSecretReader correctly updates dynamic generic secret.
TEST_F(FilterTest, SdsDynamicGenericSecret) {
  NiceMock<Server::MockConfigTracker> config_tracker;
  Secret::SecretManagerImpl secret_manager{config_tracker};
  envoy::config::core::v3::ConfigSource config_source;

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> secret_context;
  NiceMock<LocalInfo::MockLocalInfo> local_info;
  Api::ApiPtr api = Api::createApiForTest();
  NiceMock<Init::MockManager> init_manager;
  Init::TargetHandlePtr init_handle;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(secret_context.server_context_, localInfo()).WillRepeatedly(ReturnRef(local_info));
  EXPECT_CALL(secret_context.server_context_, api()).WillRepeatedly(ReturnRef(*api));
  EXPECT_CALL(secret_context.server_context_, mainThreadDispatcher())
      .WillRepeatedly(ReturnRef(dispatcher));
  EXPECT_CALL(secret_context, initManager()).Times(0);
  EXPECT_CALL(init_manager, add(_))
      .WillRepeatedly(Invoke([&init_handle](const Init::Target& target) {
        init_handle = target.createHandle("test");
      }));

  auto certificate_secret_provider = secret_manager.findOrCreateGenericSecretProvider(
      config_source, "certificate", secret_context, init_manager);
  auto certificate_callback = secret_context.cluster_manager_.subscription_factory_.callbacks_;
  auto private_key_secret_provider = secret_manager.findOrCreateGenericSecretProvider(
      config_source, "private_key", secret_context, init_manager);
  auto private_key_callback = secret_context.cluster_manager_.subscription_factory_.callbacks_;

  NiceMock<ThreadLocal::MockInstance> tls;
  SDSSecretReader secret_reader(std::move(certificate_secret_provider),
                                std::move(private_key_secret_provider), tls, *api);
  EXPECT_TRUE(secret_reader.certificate().empty());
  EXPECT_TRUE(secret_reader.privateKey().empty());

  const std::string yaml_client = R"YAML(
name: certificate
generic_secret:
  secret:
    inline_string: "certificate_test"
)YAML";

  envoy::extensions::transport_sockets::tls::v3::Secret typed_secret;
  TestUtility::loadFromYaml(yaml_client, typed_secret);
  const auto decoded_resources_client = TestUtility::decodeResources({typed_secret});

  EXPECT_TRUE(certificate_callback->onConfigUpdate(decoded_resources_client.refvec_, "").ok());
  EXPECT_EQ(secret_reader.certificate(), "certificate_test");
  EXPECT_EQ(secret_reader.privateKey(), "");

  const std::string yaml_token = R"YAML(
name: private_key
generic_secret:
  secret:
    inline_string: "private_key_test"
)YAML";
  TestUtility::loadFromYaml(yaml_token, typed_secret);
  const auto decoded_resources_token = TestUtility::decodeResources({typed_secret});

  EXPECT_TRUE(private_key_callback->onConfigUpdate(decoded_resources_token.refvec_, "").ok());
  EXPECT_EQ(secret_reader.certificate(), "certificate_test");
  EXPECT_EQ(secret_reader.privateKey(), "private_key_test");

  const std::string yaml_client_recheck = R"EOF(
name: certificate
generic_secret:
  secret:
    inline_string: "certificate_test_recheck"
)EOF";
  TestUtility::loadFromYaml(yaml_client_recheck, typed_secret);
  const auto decoded_resources_client_recheck = TestUtility::decodeResources({typed_secret});

  EXPECT_TRUE(
      certificate_callback->onConfigUpdate(decoded_resources_client_recheck.refvec_, "").ok());
  EXPECT_EQ(secret_reader.certificate(), "certificate_test_recheck");
  EXPECT_EQ(secret_reader.privateKey(), "private_key_test");
}

TEST_F(FilterTest, NoHostHeader) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testPassthroughHtml(request_headers, response_headers, false);
}

TEST_F(FilterTest, AcceptTextHtml) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"accept", "text/html"}, {"host", "example.org"}, {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testPassthroughHtml(request_headers, response_headers, false);
}

TEST_F(FilterTest, HtmlWithTrailers) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"accept", "text/html"}, {"host", "example.org"}, {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "text/html"},
                                                   {":status", "200"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"x-test-sample-trailer", "wait for me!"}};
  testPassthroughHtml(request_headers, response_headers, &response_trailers, false);
}

TEST_F(FilterTest, NoPathHeader) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testPassthroughHtml(request_headers, response_headers, false);
}

TEST_F(FilterTest, NoAcceptHeader) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"host", "example.org"}, {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testPassthroughHtml(request_headers, response_headers, false);
}

TEST_F(FilterTest, NoStatusHeader) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "text/html"},
                                                   {"x-should-encode-sxg", "true"}};
  testPassthroughHtml(request_headers, response_headers, true);
}

TEST_F(FilterTest, Status404) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "404"}, {"x-should-encode-sxg", "true"}};
  testPassthroughHtml(request_headers, response_headers, true);
}

TEST_F(FilterTest, XShouldEncodeNotSet) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"host", "example.org"},
      {"accept", "application/signed-exchange;v=b3;q=0.9,text/html;q=0.8"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "text/html"},
                                                   {":status", "200"}};
  testPassthroughHtml(request_headers, response_headers, true);
}

TEST_F(FilterTest, AcceptTextHtmlWithQ) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "text/html;q=0.8"},
                                                 {":protocol", "https"},
                                                 {":host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testPassthroughHtml(request_headers, response_headers, false);
}

TEST_F(FilterTest, AcceptApplicationSignedExchangeNoVersion) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"accept", "application/signed-exchange"}, {"host", "example.org"}, {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testPassthroughHtml(request_headers, response_headers, false);
}

TEST_F(FilterTest, AcceptApplicationSignedExchangeWithVersionB2) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b2"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testPassthroughHtml(request_headers, response_headers, false);
}

TEST_F(FilterTest, AcceptApplicationSignedExchangeWithVersionB3) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testEncodeSignedExchange(request_headers, response_headers);
}

TEST_F(FilterTest, AcceptApplicationSignedExchangeWithVersionB3WithQ) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"accept", "application/signed-exchange;v=b3;q=0.9"},
      {"host", "example.org"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testEncodeSignedExchange(request_headers, response_headers);
}

TEST_F(FilterTest, AcceptMultipleTextHtml) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"host", "example.org"},
      {"accept", "application/signed-exchange;v=b3;q=0.8,text/html;q=0.9"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testPassthroughHtml(request_headers, response_headers, false);
}

TEST_F(FilterTest, AcceptMultipleSignedExchange) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"host", "example.org"},
      {"accept", "application/signed-exchange;v=b3;q=0.9,text/html;q=0.8"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testEncodeSignedExchange(request_headers, response_headers);
}

TEST_F(FilterTest, ResponseExceedsMaxPayloadSize) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"host", "example.org"},
      {"accept", "application/signed-exchange;v=b3;q=0.9,text/html;q=0.8"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  EXPECT_CALL(encoder_callbacks_, encoderBufferLimit).WillRepeatedly(Return(10));
  testFallbackToHtml(request_headers, response_headers, true, false);
}

TEST_F(FilterTest, ResponseExceedsMaxPayloadSizeEncodeFail) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"host", "example.org"},
      {"accept", "application/signed-exchange;v=b3;q=0.9,text/html;q=0.8"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  EXPECT_CALL(encoder_callbacks_, encoderBufferLimit)
      .WillOnce(Return(100000))
      .WillRepeatedly(Return(10));
  testFallbackToHtml(request_headers, response_headers, true, true);
}

TEST_F(FilterTest, UrlWithQueryParam) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"accept", "application/signed-exchange;v=b3;q=0.9"},
      {"host", "example.org"},
      {":path", "/hello.html?good=bye"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  const Buffer::OwnedImpl expected_sxg(
      "sxg1-b3\0\0\x27https://example.org/hello.html?good=bye\0\x1\0\0\0\x84"
      "label;cert-sha256=*unJ3rwJT2DwWlJAw1lfVLvPjeYoJh0+QUQ97zJQPZtc=*;cert-url=\"https://"
      "example.org/.sxg/"
      "cert.cbor?d=ba7277af0253d83c\";date=1610416640;expires=1611021440;integrity=\"digest/"
      "mi-sha256-03\";sig=**;validity-url=\"https://example.org/.sxg/"
      "validity.msg\"\xA4"
      "FdigestX9mi-sha256-03=0x0E2wkWVYOJ7Gq8+Kfaiyjo3gYCyaijhGGgkzjPoTo=G:statusC200Lcontent-"
      "typeItext/htmlPcontent-encodingLmi-sha256-03\0\0\0\0\0\0\x10\0<html><body>hi!</body></"
      "html>\n",
      481);
  testEncodeSignedExchange(request_headers, response_headers, expected_sxg);
}

TEST_F(FilterTest, CborValdityFullUrls) {
  setConfiguration({R"YAML(
cbor_url: "https://amp.example.org/cert.cbor"
validity_url: "https://amp.example.org/validity.msg"
)YAML"});
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  const Buffer::OwnedImpl expected_sxg(
      "sxg1-b3\0\0\x1Ehttps://example.org/hello.html\0\0\xFE\0\0\x84"
      "label;cert-sha256=*unJ3rwJT2DwWlJAw1lfVLvPjeYoJh0+QUQ97zJQPZtc=*;cert-url=\"https://"
      "amp.example.org/"
      "cert.cbor?d=ba7277af0253d83c\";date=1610416640;expires=1611021440;integrity=\"digest/"
      "mi-sha256-03\";sig=**;validity-url=\"https://amp.example.org/"
      "validity.msg\"\xA4"
      "FdigestX9mi-sha256-03=0x0E2wkWVYOJ7Gq8+Kfaiyjo3gYCyaijhGGgkzjPoTo=G:statusC200Lcontent-"
      "typeItext/htmlPcontent-encodingLmi-sha256-03\0\0\0\0\0\0\x10\0<html><body>hi!</body></"
      "html>\n",
      470);
  testEncodeSignedExchange(request_headers, response_headers, expected_sxg);
}

TEST_F(FilterTest, WithHttpTrailers) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"accept", "application/signed-exchange;v=b3;q=0.9"},
      {"host", "example.org"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  Http::TestResponseTrailerMapImpl response_trailers{{"x-test-sample-trailer", "wait for me!"}};
  testEncodeSignedExchange(request_headers, response_headers, &response_trailers);
}

TEST_F(FilterTest, WithCustomShouldEncodeHeader) {
  setConfiguration({R"YAML(
cbor_url: "/.sxg/cert.cbor"
validity_url: "/.sxg/validity.msg"
should_encode_sxg_header: "x-custom-should-encode-sxg"
)YAML"});
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{
      {"accept", "application/signed-exchange;v=b3;q=0.9"},
      {"host", "example.org"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-custom-should-encode-sxg", "true"}};
  testEncodeSignedExchange(request_headers, response_headers);
}

TEST_F(FilterTest, FilterXEnvoyHeaders) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "text/html"},
                                                   {":status", "200"},
                                                   {"x-should-encode-sxg", "true"},
                                                   {"x-envoy-something", "something"}};
  testEncodeSignedExchange(request_headers, response_headers);
}

TEST_F(FilterTest, FilterCustomHeaders) {
  setConfiguration({R"YAML(
cbor_url: "/.sxg/cert.cbor"
validity_url: "/.sxg/validity.msg"
header_prefix_filters:
 - "x-foo-"
 - "x-bar-"
)YAML"});
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "text/html"},
                                                   {":status", "200"},
                                                   {"x-should-encode-sxg", "true"},
                                                   {"x-foo-bar", "foo"},
                                                   {"x-bar-baz", "bar"}};
  testEncodeSignedExchange(request_headers, response_headers);
  const Envoy::Http::LowerCaseString x_client_can_accept_sxg_key("x-client-can-accept-sxg");
}

TEST_F(FilterTest, CustomHeader) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "text/html"},
                                                   {":status", "200"},
                                                   {"x-should-encode-sxg", "true"},
                                                   {"x-special-header", "very special"}};
  const Buffer::OwnedImpl expected_sxg(
      "sxg1-b3\0\0\x1Ehttps://example.org/hello.html\0\x1\0\0\0\xA2"
      "label;cert-sha256=*unJ3rwJT2DwWlJAw1lfVLvPjeYoJh0+QUQ97zJQPZtc=*;cert-url=\"https://"
      "example.org/.sxg/"
      "cert.cbor?d=ba7277af0253d83c\";date=1610416640;expires=1611021440;integrity=\"digest/"
      "mi-sha256-03\";sig=**;validity-url=\"https://example.org/.sxg/"
      "validity.msg\"\xA5"
      "FdigestX9mi-sha256-03=0x0E2wkWVYOJ7Gq8+Kfaiyjo3gYCyaijhGGgkzjPoTo=G:statusC200Lcontent-"
      "typeItext/htmlPcontent-encodingLmi-sha256-03Px-special-headerLvery special"
      "\0\0\0\0\0\0\x10\0<html><body>hi!</body></html>\n",
      502);
  testEncodeSignedExchange(request_headers, response_headers, expected_sxg);
}

TEST_F(FilterTest, ExtraHeaders) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{{"content-type", "text/html"},
                                                   {":status", "200"},
                                                   {"x-should-encode-sxg", "true"},
                                                   {"x-special-header", "twice"},
                                                   {"x-special-header", "as special"}};
  const Buffer::OwnedImpl expected_sxg(
      "sxg1-b3\0\0\x1Ehttps://example.org/hello.html\0\x1\x0\0\0\xA6"
      "label;cert-sha256=*unJ3rwJT2DwWlJAw1lfVLvPjeYoJh0+QUQ97zJQPZtc=*;cert-url=\"https://"
      "example.org/.sxg/"
      "cert.cbor?d=ba7277af0253d83c\";date=1610416640;expires=1611021440;integrity=\"digest/"
      "mi-sha256-03\";sig=**;validity-url=\"https://example.org/.sxg/"
      "validity.msg\"\xA5"
      "FdigestX9mi-sha256-03=0x0E2wkWVYOJ7Gq8+Kfaiyjo3gYCyaijhGGgkzjPoTo=G:statusC200Lcontent-"
      "typeItext/htmlPcontent-encodingLmi-sha256-03Px-special-headerP"
      "twice,as special"
      "\0\0\0\0\0\0\x10\0<html><body>hi!</body></html>\n",
      506);

  testEncodeSignedExchange(request_headers, response_headers, expected_sxg);
}

TEST_F(FilterTest, TestDoubleDoSxg) {
  setConfiguration();
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testEncodeSignedExchange(request_headers, response_headers);
  callDoSxgAgain();
}

TEST_F(FilterTest, LoadHeadersFailure) {
  setConfiguration();
  encoder_ = std::make_unique<MockEncoder>();
  setFilter();
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), setOrigin);
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), setUrl);
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadHeaders).WillOnce(Return(false));

  Http::TestRequestHeaderMapImpl request_headers{
      {"host", "example.org"},
      {"accept", "application/signed-exchange;v=b3;q=0.9,text/html;q=0.8"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testFallbackToHtml(request_headers, response_headers, false, true);
}

TEST_F(FilterTest, LoadContentFailure) {
  setConfiguration();
  encoder_ = std::make_unique<MockEncoder>();
  setFilter();
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), setOrigin);
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), setUrl);
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadHeaders).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadContent).WillOnce(Return(false));

  Http::TestRequestHeaderMapImpl request_headers{
      {"host", "example.org"},
      {"accept", "application/signed-exchange;v=b3;q=0.9,text/html;q=0.8"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testFallbackToHtml(request_headers, response_headers, false, true);
}

TEST_F(FilterTest, GetEncodedResponseFailure) {
  setConfiguration();
  encoder_ = std::make_unique<MockEncoder>();
  setFilter();
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), setOrigin);
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), setUrl);
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadHeaders).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadContent).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), getEncodedResponse)
      .WillOnce(Return(false));

  Http::TestRequestHeaderMapImpl request_headers{
      {"host", "example.org"},
      {"accept", "application/signed-exchange;v=b3;q=0.9,text/html;q=0.8"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testFallbackToHtml(request_headers, response_headers, false, true);
}

TEST_F(FilterTest, LoadSignerFailure) {
  setConfiguration();
  encoder_ = std::make_unique<MockEncoder>();
  setFilter();
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), setOrigin);
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), setUrl);
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadHeaders).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadContent).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), getEncodedResponse)
      .WillOnce(Return(true));
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadSigner).WillOnce(Return(false));

  Http::TestRequestHeaderMapImpl request_headers{
      {"host", "example.org"},
      {"accept", "application/signed-exchange;v=b3;q=0.9,text/html;q=0.8"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testFallbackToHtml(request_headers, response_headers, false, true);
}

TEST_F(FilterTest, WriteSxgFailure) {
  setConfiguration();
  encoder_ = std::make_unique<MockEncoder>();
  setFilter();
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), setOrigin);
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), setUrl);
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadHeaders).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadContent).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), getEncodedResponse)
      .WillOnce(Return(true));
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), loadSigner).WillOnce(Return(true));
  EXPECT_CALL(*static_cast<MockEncoder*>(encoder_.get()), writeSxg).WillOnce(Return(nullptr));

  Http::TestRequestHeaderMapImpl request_headers{
      {"host", "example.org"},
      {"accept", "application/signed-exchange;v=b3;q=0.9,text/html;q=0.8"},
      {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testFallbackToHtml(request_headers, response_headers, false, true);
}

// MyCombinedCertKeyId
TEST_F(FilterTest, CombiedCertificateId) {
  const std::string certificate(R"PEM(
-----BEGIN CERTIFICATE-----
MIIBhjCCASygAwIBAgIJAIH9REPqIFXTMAkGByqGSM49BAEwMjEUMBIGA1UEAwwL
ZXhhbXBsZS5vcmcxDTALBgNVBAoMBFRlc3QxCzAJBgNVBAYTAlVTMB4XDTIxMDEx
MzAxMDcwMVoXDTIxMDQxMzAxMDcwMVowMjEUMBIGA1UEAwwLZXhhbXBsZS5vcmcx
DTALBgNVBAoMBFRlc3QxCzAJBgNVBAYTAlVTMFkwEwYHKoZIzj0CAQYIKoZIzj0D
AQcDQgAE4ZrHsGLEiP+pV70a8zIERNcu9MBJHHfbeqLUqwGWWU2/YHObf58nE9to
c6lgrko2JdbV6TyWLVUc/M0Pn+OVSaMsMCowEAYKKwYBBAHWeQIBFgQCBQAwFgYD
VR0RBA8wDYILZXhhbXBsZS5vcmcwCQYHKoZIzj0EAQNJADBGAiEAuQJjX+z7j4hR
xtxfs4VPY5RsF5Sawd+mtluRxpoURcsCIQCIGU/11jcuS0UbIpt4B5Gb1UJlSKGi
Dgu+2OKt7qVPrA==
-----END CERTIFICATE-----
-----BEGIN EC PARAMETERS-----
BggqhkjOPQMBBw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIJyGXecxIQtBwBJWU4Sc5A8UHNt5HnOBR9Oh11AGYa/2oAoGCCqGSM49
AwEHoUQDQgAE4ZrHsGLEiP+pV70a8zIERNcu9MBJHHfbeqLUqwGWWU2/YHObf58n
E9toc6lgrko2JdbV6TyWLVUc/M0Pn+OVSQ==
-----END EC PRIVATE KEY-----
)PEM");

  setConfiguration({R"YAML(
cbor_url: "/.sxg/cert.cbor"
validity_url: "/.sxg/validity.msg"
)YAML"},
                   certificate, certificate);
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"accept", "application/signed-exchange;v=b3"},
                                                 {"host", "example.org"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};
  testEncodeSignedExchange(request_headers, response_headers);
}

TEST_F(FilterTest, BadCertificateId) {
  const std::string certificate("");
  const std::string private_key(R"PEM(
-----BEGIN EC PARAMETERS-----
BggqhkjOPQMBBw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIJyGXecxIQtBwBJWU4Sc5A8UHNt5HnOBR9Oh11AGYa/2oAoGCCqGSM49
AwEHoUQDQgAE4ZrHsGLEiP+pV70a8zIERNcu9MBJHHfbeqLUqwGWWU2/YHObf58n
E9toc6lgrko2JdbV6TyWLVUc/M0Pn+OVSQ==
-----END EC PRIVATE KEY-----
)PEM");

  setConfiguration({R"YAML(
cbor_url: "/.sxg/cert.cbor"
validity_url: "/.sxg/validity.msg"
)YAML"},
                   certificate, private_key);
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"host", "example.org"},
                                                 {"accept", "application/signed-exchange;v=b3"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};

  testFallbackToHtml(request_headers, response_headers, false, true);
}
std::string private_key(R"PEM(
-----BEGIN EC PARAMETERS-----
BggqhkjOPQMBBw==
-----END EC PARAMETERS-----
-----BEGIN EC PRIVATE KEY-----
MHcCAQEEIJyGXecxIQtBwBJWU4Sc5A8UHNt5HnOBR9Oh11AGYa/2oAoGCCqGSM49
AwEHoUQDQgAE4ZrHsGLEiP+pV70a8zIERNcu9MBJHHfbeqLUqwGWWU2/YHObf58n
E9toc6lgrko2JdbV6TyWLVUc/M0Pn+OVSQ==
-----END EC PRIVATE KEY-----
)PEM");

TEST_F(FilterTest, BadPriKeyId) {
  const std::string certificate(R"PEM(
-----BEGIN CERTIFICATE-----
MIIBhjCCASygAwIBAgIJAIH9REPqIFXTMAkGByqGSM49BAEwMjEUMBIGA1UEAwwL
ZXhhbXBsZS5vcmcxDTALBgNVBAoMBFRlc3QxCzAJBgNVBAYTAlVTMB4XDTIxMDEx
MzAxMDcwMVoXDTIxMDQxMzAxMDcwMVowMjEUMBIGA1UEAwwLZXhhbXBsZS5vcmcx
DTALBgNVBAoMBFRlc3QxCzAJBgNVBAYTAlVTMFkwEwYHKoZIzj0CAQYIKoZIzj0D
AQcDQgAE4ZrHsGLEiP+pV70a8zIERNcu9MBJHHfbeqLUqwGWWU2/YHObf58nE9to
c6lgrko2JdbV6TyWLVUc/M0Pn+OVSaMsMCowEAYKKwYBBAHWeQIBFgQCBQAwFgYD
VR0RBA8wDYILZXhhbXBsZS5vcmcwCQYHKoZIzj0EAQNJADBGAiEAuQJjX+z7j4hR
xtxfs4VPY5RsF5Sawd+mtluRxpoURcsCIQCIGU/11jcuS0UbIpt4B5Gb1UJlSKGi
Dgu+2OKt7qVPrA==
-----END CERTIFICATE-----
)PEM");
  const std::string private_key("");

  setConfiguration({R"YAML(
cbor_url: "/.sxg/cert.cbor"
validity_url: "/.sxg/validity.msg"
)YAML"},
                   certificate, private_key);
  setFilter();

  Http::TestRequestHeaderMapImpl request_headers{{"host", "example.org"},
                                                 {"accept", "application/signed-exchange;v=b3"},
                                                 {":path", "/hello.html"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {"content-type", "text/html"}, {":status", "200"}, {"x-should-encode-sxg", "true"}};

  testFallbackToHtml(request_headers, response_headers, false, true);
}

} // namespace SXG
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
