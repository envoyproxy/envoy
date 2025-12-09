#pragma once

#include "envoy/geoip/geoip_provider_driver.h"

#include "test/extensions/filters/http/geoip/dummy.pb.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Geoip {

class MockDriver : public Geolocation::Driver {
public:
  MOCK_METHOD(void, lookup,
              (Geolocation::LookupRequest && request, Geolocation::LookupGeoHeadersCallback&& cb),
              (const, override));
};

using MockDriverSharedPtr = std::shared_ptr<MockDriver>;

class DummyGeoipProviderFactory : public Geolocation::GeoipProviderFactory {
public:
  DummyGeoipProviderFactory() : driver_(std::make_shared<MockDriver>()) {}

  Geolocation::DriverSharedPtr
  createGeoipProviderDriver(const Protobuf::Message&, const std::string&,
                            Server::Configuration::FactoryContext&) override {
    return driver_;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<test::extensions::filters::http::geoip::DummyProvider>();
  }

  std::string name() const override { return "envoy.geoip_providers.dummy"; }

  MockDriverSharedPtr getDriver() { return driver_; }

private:
  MockDriverSharedPtr driver_;
};

} // namespace Geoip
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
