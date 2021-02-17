#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;

namespace Envoy {
namespace Formatter {

class OriginalIPDetectionIntegrationTest : public testing::Test, public HttpIntegrationTest {
public:
  OriginalIPDetectionIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, Network::Address::IpVersion::v4) {}
};

class CustomHeaderDetection : public Http::OriginalIPDetection {
public:
  CustomHeaderDetection(const std::string& header_name) : header_name_(header_name) {}

  Http::OriginalIPDetectionResult detect(Http::OriginalIPDetectionParams& params) override {
    auto hdr = params.request_headers.get(Http::LowerCaseString(header_name_));
    if (hdr.empty()) {
      return {nullptr, false, absl::nullopt};
    }
    auto header_value = hdr[0]->value().getStringView();
    return {std::make_shared<Network::Address::Ipv4Instance>(std::string(header_value)), false,
            absl::nullopt};
  }

private:
  std::string header_name_;
};

class HeaderDetectionFactory : public Http::OriginalIPDetectionFactory {
public:
  Http::OriginalIPDetectionSharedPtr createExtension(const Protobuf::Message&) const override {
    return std::make_shared<CustomHeaderDetection>("x-cdn-detected-ip");
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtobufWkt::StringValue>();
  }
  std::string name() const override { return "CustomHeaderDetection"; }
};

TEST_F(OriginalIPDetectionIntegrationTest, HeaderBasedDetection) {
  HeaderDetectionFactory factory;
  Registry::InjectFactory<Http::OriginalIPDetectionFactory> header_detection_factory_register(
      factory);

  useAccessLog("%DOWNSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%");
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        ProtobufWkt::StringValue config;

        auto* original_ip_detection = hcm.mutable_original_ip_detection_extensions()->Add();
        original_ip_detection->set_name("CustomHeaderDetection");
        original_ip_detection->mutable_typed_config()->PackFrom(config);

        hcm.mutable_use_remote_address()->set_value(false);
      });
  initialize();
  std::string response;
  sendRawHttpAndWaitForResponse(
      lookupPort("http"), "GET / HTTP/1.1\r\nHost: host\r\nx-cdn-detected-ip: 9.9.9.9\r\n\r\n",
      &response, true);
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, HasSubstr("9.9.9.9"));
}

} // namespace Formatter
} // namespace Envoy
