#pragma once

#include "envoy/geoip/geoip_provider_driver.h"

#include "test/extensions/filters/http/geoip/dummy.pb.h"
#include "test/extensions/filters/http/geoip/dummy.pb.validate.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

class MockDriver : public Geolocation::Driver {
public:
  MOCK_METHOD(void, lookup,
              (Geolocation::LookupRequest && request, Geolocation::LookupGeoHeadersCallback&&),
              (const));
};

using MockDriverSharedPtr = std::shared_ptr<MockDriver>;

class DummyGeoipProviderFactory : public Geolocation::GeoipProviderFactory {
public:
  DummyGeoipProviderFactory() : driver_(new MockDriver()) {}
  Geolocation::DriverSharedPtr
  createGeoipProviderDriver(const Protobuf::Message&, const std::string&,
                            Server::Configuration::FactoryContext&) override {
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
