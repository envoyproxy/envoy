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
  MOCK_METHOD(absl::Span<const Http::LowerCaseString>, geoHeaderNames, (), (const));
};

using MockDriverSharedPtr = std::shared_ptr<MockDriver>;

class DummyGeoipProviderFactory : public GeoipProviderFactory {
public:
  // A NiceMock so that tests which do not care about geoHeaderNames() are not warned about the
  // call the HTTP filter makes on every request; it returns an empty span by default.
  DummyGeoipProviderFactory() : driver_(new testing::NiceMock<MockDriver>()) {}
  Geolocation::DriverSharedPtr
  createGeoipProviderDriver(const Protobuf::Message&, const std::string&,
                            Server::Configuration::ServerFactoryContext&) override {
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
