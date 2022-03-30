#include "library/common/extensions/key_value/platform/config.h"

#include "envoy/config/common/key_value/v3/config.pb.h"
#include "envoy/config/common/key_value/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace KeyValue {

PlatformKeyValueStore::PlatformKeyValueStore(Event::Dispatcher& dispatcher,
                                             std::chrono::milliseconds save_interval,
                                             PlatformInterface& platform_interface,
                                             const std::string& key)
    : KeyValueStoreBase(dispatcher, save_interval), platform_interface_(platform_interface),
      key_(key) {
  const std::string contents = platform_interface.read(key);
  if (!parseContents(contents, store_)) {
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
  return std::make_unique<PlatformKeyValueStore>(
      dispatcher, milliseconds, platform_interface_.value().get(), file_config.key());
}

REGISTER_FACTORY(PlatformKeyValueStoreFactory, KeyValueStoreFactory);

} // namespace KeyValue
} // namespace Extensions
} // namespace Envoy
