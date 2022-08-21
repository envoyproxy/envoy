#pragma once

#include "envoy/common/key_value_store.h"
#include "envoy/config/xds_resources_delegate.h"

namespace Envoy {
namespace Extensions {
namespace Config {

// An implementation of the XdsResourcesDelegate interface that saves and retrieves xDS resources
// to/from the configured KeyValueStore implementation.
//
// The configured KeyValueStore should not do any storage or network I/O on the main thread in their
// addOrUpdate() and get() implementations. Doing so would cause the main thread to block, since
// the delegate is invoked on the main Envoy thread.
class KeyValueStoreXdsDelegate : public Envoy::Config::XdsResourcesDelegate {
public:
  KeyValueStoreXdsDelegate(KeyValueStorePtr&& xds_config_store);

  std::vector<envoy::service::discovery::v3::Resource>
  getResources(const Envoy::Config::XdsSourceId& source_id,
               const std::vector<std::string>& resource_names) const override;

  void onConfigUpdated(const Envoy::Config::XdsSourceId& source_id,
                       const std::vector<Envoy::Config::DecodedResourceRef>& resources) override;

private:
  // Gets all the resources present in the KeyValueStore for the given source_id. This is the
  // equivalent of wildcard xDS requests.
  std::vector<envoy::service::discovery::v3::Resource>
  getAllResources(const Envoy::Config::XdsSourceId& source_id) const;

  KeyValueStorePtr xds_config_store_;
};

// A factory for creating instances of KeyValueStoreXdsDelegate from the typed_config field of a
// TypedExtensionConfig protocol buffer message.
class KeyValueStoreXdsDelegateFactory : public Envoy::Config::XdsResourcesDelegateFactory {
public:
  KeyValueStoreXdsDelegateFactory() = default;

  Envoy::ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string name() const override;

  Envoy::Config::XdsResourcesDelegatePtr
  createXdsResourcesDelegate(const ProtobufWkt::Any& config,
                             ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
                             Event::Dispatcher& dispatcher) override;
};

} // namespace Config
} // namespace Extensions
} // namespace Envoy
