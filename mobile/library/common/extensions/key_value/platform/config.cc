#include "library/common/extensions/key_value/platform/config.h"

#include "envoy/config/common/key_value/v3/config.pb.h"
#include "envoy/config/common/key_value/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "library/common/api/external.h"
#include "library/common/data/utility.h"
#include "library/common/extensions/key_value/platform/c_types.h"

namespace Envoy {
namespace Extensions {
namespace KeyValue {

class PlatformInterfaceImpl : PlatformInterface, public Logger::Loggable<Logger::Id::filter> {
public:
  PlatformInterfaceImpl(const std::string& name)
      : bridged_store_(*static_cast<envoy_kv_store*>(Api::External::retrieveApi(name))) {}

  ~PlatformInterfaceImpl() override {}

  std::string read(const std::string& key) const override {
    envoy_data bridged_key = Data::Utility::copyToBridgeData(key);
    envoy_data bridged_value = bridged_store_.read(bridged_key, bridged_store_.context);
    return Data::Utility::copyToString(bridged_value);
  }

  void save(const std::string& key, const std::string& contents) override {
    envoy_data bridged_key = Data::Utility::copyToBridgeData(key);
    envoy_data bridged_value = Data::Utility::copyToBridgeData(contents);
    bridged_store_.save(bridged_key, bridged_value, bridged_store_.context);
  }

private:
  envoy_kv_store bridged_store_;
};

PlatformKeyValueStore::PlatformKeyValueStore(Event::Dispatcher& dispatcher,
                                             std::chrono::milliseconds save_interval,
                                             PlatformInterface& platform_interface,
                                             uint64_t max_entries, const std::string& key)
    : KeyValueStoreBase(dispatcher, save_interval, max_entries),
      platform_interface_(platform_interface), key_(key) {
  const std::string contents = platform_interface.read(key);
  if (!parseContents(contents)) {
    ENVOY_LOG(warn, "Failed to parse key value store contents {}", key);
  }
}

void PlatformKeyValueStore::flush() {
  std::string output;
  for (const auto& it : store_) {
    absl::StrAppend(&output, it.first.length(), "\n", it.first, it.second.length(), "\n",
                    it.second);
  }
  platform_interface_.save(key_, output);
}

KeyValueStorePtr
PlatformKeyValueStoreFactory::createStore(const Protobuf::Message& config,
                                          ProtobufMessage::ValidationVisitor& validation_visitor,
                                          Event::Dispatcher& dispatcher, Filesystem::Instance&) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const ::envoy::config::common::key_value::v3::KeyValueStoreConfig&>(config,
                                                                          validation_visitor);
  const auto file_config = MessageUtil::anyConvertAndValidate<
      envoymobile::extensions::key_value::platform::PlatformKeyValueStoreConfig>(
      typed_config.config().typed_config(), validation_visitor);
  auto milliseconds =
      std::chrono::milliseconds(DurationUtil::durationToMilliseconds(file_config.save_interval()));
  return std::make_unique<PlatformKeyValueStore>(dispatcher, milliseconds,
                                                 platform_interface_.value().get(),
                                                 file_config.max_entries(), file_config.key());
}

REGISTER_FACTORY(PlatformKeyValueStoreFactory, KeyValueStoreFactory);

} // namespace KeyValue
} // namespace Extensions
} // namespace Envoy
