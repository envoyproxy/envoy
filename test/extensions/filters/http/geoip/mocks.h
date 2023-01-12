#include "source/extensions/filters/http/geoip/geoip_provider_config.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Geoip {

class MockDriver : public Driver {
public:
  MOCK_METHOD(void, getCity,
              (const Network::Address::InstanceConstSharedPtr&, const LookupCallbacks&,
               const absl::optional<std::string>&),
              (const));
  MOCK_METHOD(void, getCountry,
              (const Network::Address::InstanceConstSharedPtr&, const LookupCallbacks&,
               const absl::optional<std::string>&),
              (const));
  MOCK_METHOD(void, getRegion,
              (const Network::Address::InstanceConstSharedPtr&, const LookupCallbacks&,
               const absl::optional<std::string>&),
              (const));
  MOCK_METHOD(void, getAsn,
              (const Network::Address::InstanceConstSharedPtr&, const LookupCallbacks&,
               const absl::optional<std::string>&),
              (const));
  MOCK_METHOD(void, getIsAnonymous,
              (const Network::Address::InstanceConstSharedPtr&, const LookupCallbacks&,
               const absl::optional<std::string>&),
              (const));
  MOCK_METHOD(void, getIsAnonymousVpn,
              (const Network::Address::InstanceConstSharedPtr&, const LookupCallbacks&,
               const absl::optional<std::string>&),
              (const));
  MOCK_METHOD(void, getIsAnonymousHostingProvider,
              (const Network::Address::InstanceConstSharedPtr&, const LookupCallbacks&,
               const absl::optional<std::string>&),
              (const));
  MOCK_METHOD(void, getIsAnonymousPublicProxy,
              (const Network::Address::InstanceConstSharedPtr&, const LookupCallbacks&,
               const absl::optional<std::string>&),
              (const));
  MOCK_METHOD(void, getIsAnonymousTorExitNode,
              (const Network::Address::InstanceConstSharedPtr&, const LookupCallbacks&,
               const absl::optional<std::string>&),
              (const));
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
