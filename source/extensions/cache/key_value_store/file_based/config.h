#include "envoy/common/key_value_store.h"
#include "envoy/extensions/cache/key_value_cache/v3/config.pb.h"
#include "envoy/extensions/cache/key_value_cache/v3/config.pb.validate.h"

#include "source/common/common/key_value_store_base.h"

namespace Envoy {
namespace Extensions {
namespace Cache {
namespace KeyValueCache {

// A filesystem based key value store, which loads from and flushes to the file
// provided.
//
// All keys and values are flushed to a single file as
// [length]\n[key][length]\n[value]
class FileBasedKeyValueStore : public KeyValueStoreBase {
public:
  FileBasedKeyValueStore(Event::Dispatcher& dispatcher, std::chrono::seconds flush_interval,
                         Filesystem::Instance& file_system, const std::string& filename);
  // KeyValueStore
  void flush() override;

private:
  Filesystem::Instance& file_system_;
  const std::string filename_;
};

class FileBasedKeyValueStoreFactory : public KeyValueStoreFactory {
public:
  // KeyValueStoreFactory
  KeyValueStorePtr createStore(const Protobuf::Message& config,
                               ProtobufMessage::ValidationVisitor& validation_visitor,
                               Event::Dispatcher& dispatcher,
                               Filesystem::Instance& file_system) override;

  // TypedFactory
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::cache::key_value_cache::v3::FileBasedKeyValueCacheConfig()};
  }

  std::string name() const override { return "envoy.cache.key_value_cache.file_based_cache"; }
};

} // namespace KeyValueCache
} // namespace Cache
} // namespace Extensions
} // namespace Envoy
