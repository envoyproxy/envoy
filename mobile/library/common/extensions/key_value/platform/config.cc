#include "library/common/extensions/key_value/platform/config.h"

#include "envoy/config/common/key_value/v3/config.pb.h"
#include "envoy/config/common/key_value/v3/config.pb.validate.h"

#include "library/common/api/external.h"
#include "library/common/data/utility.h"
#include "library/common/extensions/key_value/platform/c_types.h"

namespace Envoy {
namespace Extensions {
namespace KeyValue {

namespace {

constexpr absl::string_view PlatformStoreName{"reserved.platform_store"};

class PlatformInterfaceImpl : public PlatformInterface,
                              public Logger::Loggable<Logger::Id::filter> {
public:
  PlatformInterfaceImpl()
      : bridged_store_(*static_cast<envoy_kv_store*>(
            Api::External::retrieveApi(std::string(PlatformStoreName)))) {}

  ~PlatformInterfaceImpl() override {}

  std::string read(const std::string& key) const override {
    envoy_data bridged_key = Data::Utility::copyToBridgeData(key);
    envoy_data bridged_value = bridged_store_.read(bridged_key, bridged_store_.context);
    std::string result = Data::Utility::copyToString(bridged_value);
    release_envoy_data(bridged_value);
    return result;
  }

  void save(const std::string& key, const std::string& contents) override {
    envoy_data bridged_key = Data::Utility::copyToBridgeData(key);
    envoy_data bridged_value = Data::Utility::copyToBridgeData(contents);
    bridged_store_.save(bridged_key, bridged_value, bridged_store_.context);
  }

private:
  envoy_kv_store& bridged_store_;
};

PlatformInterfaceImpl& getPlatformInterfaceImplSingleton() {
  static PlatformInterfaceImpl impl = PlatformInterfaceImpl();
  return impl;
}

} // namespace

PlatformKeyValueStore::PlatformKeyValueStore(Event::Dispatcher& dispatcher,
                                             std::chrono::milliseconds save_interval,
                                             PlatformInterface& platform_interface,
                                             uint64_t max_entries, const std::string& key)
    : KeyValueStoreBase(dispatcher, save_interval, max_entries),
      platform_interface_(platform_interface), key_(key) {
  const std::string contents = platform_interface_.read(key);
  if (!parseContents(contents)) {
    ENVOY_LOG(warn, "Failed to parse key value store contents {}", key);
  }
}

void PlatformKeyValueStore::flush() {
  std::string output;
  for (const auto& [key, value_with_ttl] : store()) {
    std::string string_value = value_with_ttl.value_;
    absl::StrAppend(&output, key.length(), "\n", key, string_value.length(), "\n", string_value);
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
  const auto platform_kv_store_config = MessageUtil::anyConvertAndValidate<
      envoymobile::extensions::key_value::platform::PlatformKeyValueStoreConfig>(
      typed_config.config().typed_config(), validation_visitor);
  auto milliseconds = std::chrono::milliseconds(
      DurationUtil::durationToMilliseconds(platform_kv_store_config.save_interval()));
  return std::make_unique<PlatformKeyValueStore>(
      dispatcher, milliseconds, getPlatformInterfaceImplSingleton(),
      platform_kv_store_config.max_entries(), platform_kv_store_config.key());
}

REGISTER_FACTORY(PlatformKeyValueStoreFactory, KeyValueStoreFactory);

} // namespace KeyValue
} // namespace Extensions
} // namespace Envoy
