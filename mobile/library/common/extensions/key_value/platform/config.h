#include "envoy/common/key_value_store.h"

#include "source/common/common/key_value_store_base.h"

#include "library/common/extensions/key_value/platform/platform.pb.h"
#include "library/common/extensions/key_value/platform/platform.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace KeyValue {

class PlatformInterface {
public:
  virtual ~PlatformInterface() {}
  // Save the contents to the key provided. This may be done asynchronously.
  virtual void save(const std::string& key, const std::string& contents) PURE;
  // Read the contents of the key provided.
  virtual std::string read(const std::string& key) const PURE;
};

// A platform file based key value store, which reads from and saves from based on
// a key. An example implementation would be flushing to the android prefs file.
//
// All keys and values are flushed to a single entry as
// [length]\n[key][length]\n[value]
class PlatformKeyValueStore : public KeyValueStoreBase {
public:
  PlatformKeyValueStore(Event::Dispatcher& dispatcher, std::chrono::milliseconds save_interval,
                        PlatformInterface& platform_interface, const std::string& key);
  // KeyValueStore
  void flush() override;

private:
  PlatformInterface& platform_interface_;
  const std::string key_;
};

class PlatformKeyValueStoreFactory : public KeyValueStoreFactory {
public:
  PlatformKeyValueStoreFactory() {}
  PlatformKeyValueStoreFactory(PlatformInterface& platform_interface)
      : platform_interface_(platform_interface) {}

  // KeyValueStoreFactory
  KeyValueStorePtr createStore(const Protobuf::Message& config,
                               ProtobufMessage::ValidationVisitor& validation_visitor,
                               Event::Dispatcher& dispatcher,
                               Filesystem::Instance& file_system) override;

  // TypedFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoymobile::extensions::key_value::platform::PlatformKeyValueStoreConfig()};
  }

  std::string name() const override { return "envoy.key_value.platform"; }
  // TODO(alyssawilk, goaway) the default PlatformInterface should do up calls through Java and this
  // can be moved to a non-optional reference.
  OptRef<PlatformInterface> platform_interface_;
};

} // namespace KeyValue
} // namespace Extensions
} // namespace Envoy
