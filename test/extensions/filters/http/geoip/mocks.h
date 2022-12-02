#include "source/extensions/filters/http/geoip/geoip_provider_config.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

class MockDriver : public Driver {
public:
  MOCK_METHOD(const absl::optional<std::string>&, getCity,
              (const Network::Address::InstanceConstSharedPtr&), (const));
  MOCK_METHOD(const absl::optional<std::string>&, getCountry,
              (const Network::Address::InstanceConstSharedPtr&), (const));
  MOCK_METHOD(const absl::optional<std::string>&, getRegion,
              (const Network::Address::InstanceConstSharedPtr&), (const));
  MOCK_METHOD(const absl::optional<std::string>&, getAsn,
              (const Network::Address::InstanceConstSharedPtr&), (const));
  MOCK_METHOD(const absl::optional<bool>&, getIsAnonymous,
              (const Network::Address::InstanceConstSharedPtr&), (const));
  MOCK_METHOD(const absl::optional<bool>&, getIsAnonymousVpn,
              (const Network::Address::InstanceConstSharedPtr&), (const));
  MOCK_METHOD(const absl::optional<bool>&, getIsAnonymousHostingProvider,
              (const Network::Address::InstanceConstSharedPtr&), (const));
  MOCK_METHOD(const absl::optional<bool>&, getIsAnonymousPublicProxy,
              (const Network::Address::InstanceConstSharedPtr&), (const));
  MOCK_METHOD(const absl::optional<bool>&, getIsAnonymousTorExitNode,
              (const Network::Address::InstanceConstSharedPtr&), (const));
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
    return std::make_unique<ProtobufWkt::Struct>();
  }

private:
  MockDriverSharedPtr driver_;
};

} // namespace Geoip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
