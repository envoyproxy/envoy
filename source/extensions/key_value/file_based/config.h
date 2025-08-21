#pragma once

#include "envoy/common/key_value_store.h"
#include "envoy/config/common/key_value/v3/config.pb.h"
#include "envoy/config/common/key_value/v3/config.pb.validate.h"
#include "envoy/extensions/key_value/file_based/v3/config.pb.h"
#include "envoy/extensions/key_value/file_based/v3/config.pb.validate.h"

#include "source/common/common/key_value_store_base.h"

namespace Envoy {
namespace Extensions {
namespace KeyValue {

// A filesystem based key value store, which loads from and flushes to the file
// provided.
//
// All keys and values are flushed to a single file as
// [length]\n[key][length]\n[value]
class FileBasedKeyValueStore : public KeyValueStoreBase {
public:
  FileBasedKeyValueStore(Event::Dispatcher& dispatcher, std::chrono::milliseconds flush_interval,
                         Filesystem::Instance& file_system, const std::string& filename,
                         uint32_t max_entries);
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
        new envoy::extensions::key_value::file_based::v3::FileBasedKeyValueStoreConfig()};
  }

  std::string name() const override { return "envoy.key_value.file_based"; }
};

} // namespace KeyValue
} // namespace Extensions
} // namespace Envoy
