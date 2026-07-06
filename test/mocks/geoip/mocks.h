#pragma once

#include "envoy/geoip/geoip_provider_driver.h"

#include "test/mocks/geoip/dummy.pb.h"
#include "test/mocks/geoip/dummy.pb.validate.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Geolocation {

class MockDriver : public Driver {
public:
  MOCK_METHOD(void, lookup,
              (Geolocation::LookupRequest && request, Geolocation::LookupGeoHeadersCallback&&),
              (const));
};

using MockDriverSharedPtr = std::shared_ptr<MockDriver>;

class DummyGeoipProviderFactory : public GeoipProviderFactory {
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
    return std::make_unique<test::mocks::geoip::DummyProvider>();
  }

private:
  MockDriverSharedPtr driver_;
};

} // namespace Geolocation
} // namespace Envoy
