#include "source/extensions/key_value/file_based/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace KeyValue {

FileBasedKeyValueStore::FileBasedKeyValueStore(Event::Dispatcher& dispatcher,
                                               std::chrono::milliseconds flush_interval,
                                               Filesystem::Instance& file_system,
                                               const std::string& filename, uint32_t max_entries)
    : KeyValueStoreBase(dispatcher, flush_interval, max_entries), file_system_(file_system),
      filename_(filename) {
  if (!file_system_.fileExists(filename_)) {
    ENVOY_LOG(info, "File for key value store does not yet exist: {}", filename);
    return;
  }
  const std::string contents = file_system_.fileReadToEnd(filename_);
  if (!parseContents(contents)) {
    ENVOY_LOG(warn, "Failed to parse key value store file {}", filename);
  }
}

void FileBasedKeyValueStore::flush() {
  static constexpr Filesystem::FlagSet DefaultFlags{1 << Filesystem::File::Operation::Write |
                                                    1 << Filesystem::File::Operation::Create};
  Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, filename_};
  auto file = file_system_.createFile(file_info);
  if (!file || !file->open(DefaultFlags).return_value_) {
    ENVOY_LOG(error, "Failed to flush cache to file {}", filename_);
    return;
  }
  for (const auto& [key, value_with_ttl] : store()) {
    file->write(absl::StrCat(key.length(), "\n"));
    file->write(key);
    file->write(absl::StrCat(value_with_ttl.value_.length(), "\n"));
    file->write(value_with_ttl.value_);
    if (value_with_ttl.ttl_.has_value()) {
      std::string ttl = std::to_string(value_with_ttl.ttl_.value().count());
      file->write(KV_STORE_TTL_KEY);
      file->write(absl::StrCat(ttl.length(), "\n"));
      file->write(ttl);
    }
  }
  file->close();
}

KeyValueStorePtr FileBasedKeyValueStoreFactory::createStore(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor,
    Event::Dispatcher& dispatcher, Filesystem::Instance& file_system) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const envoy::config::common::key_value::v3::KeyValueStoreConfig&>(config, validation_visitor);
  const auto file_config = MessageUtil::anyConvertAndValidate<
      envoy::extensions::key_value::file_based::v3::FileBasedKeyValueStoreConfig>(
      typed_config.config().typed_config(), validation_visitor);
  const auto milliseconds =
      std::chrono::milliseconds(DurationUtil::durationToMilliseconds(file_config.flush_interval()));
  const uint32_t max_entries = PROTOBUF_GET_WRAPPED_OR_DEFAULT(file_config, max_entries, 1000);
  return std::make_unique<FileBasedKeyValueStore>(dispatcher, milliseconds, file_system,
                                                  file_config.filename(), max_entries);
}

REGISTER_FACTORY(FileBasedKeyValueStoreFactory, KeyValueStoreFactory);

} // namespace KeyValue
} // namespace Extensions
} // namespace Envoy
