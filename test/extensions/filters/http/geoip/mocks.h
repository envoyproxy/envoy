#include "source/extensions/filters/http/geoip/geoip_provider_config.h"

#include "test/extensions/filters/http/geoip/dummy.pb.h"
#include "test/extensions/filters/http/geoip/dummy.pb.validate.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

class MockDriver : public Driver {
public:
  MOCK_METHOD(void, lookup, (LookupRequest && request, LookupGeoHeadersCallback&&), (const));
};

using MockDriverSharedPtr = std::shared_ptr<MockDriver>;

class DummyGeoipProviderFactory : public GeoipProviderFactory {
public:
  DummyGeoipProviderFactory() : driver_(new MockDriver()) {}
  DriverSharedPtr createGeoipProviderDriver(const Protobuf::Message&,
                                            GeoipProviderFactoryContextPtr&) override {
    return driver_;
  }

  MockDriverSharedPtr getDriver() { return driver_; }

  std::string name() const override { return "envoy.geoip_providers.dummy"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::extensions::filters::http::geoip::DummyProvider>();
  }

private:
  MockDriverSharedPtr driver_;
};

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
