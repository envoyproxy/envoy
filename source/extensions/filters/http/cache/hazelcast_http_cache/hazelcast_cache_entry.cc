#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_cache_entry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

void HazelcastHeaderEntry::writeData(ObjectDataOutput& writer) const {
  // Serialization of HeaderEntry to be written to distributed map.
  // Order of reading must be the same with this.
  writeUnifiedData(writer);
  writer.writeInt(version_);

  // Hazelcast stores signed types only.
  // Casting to signed before writing and back to unsigned when reading.
  writer.writeLong(static_cast<int64_t>(body_size_));
}

void HazelcastHeaderEntry::readData(ObjectDataInput& reader) {
  // Deserialization of HeaderEntry to be read from distributed map.
  // Order of writing must be the same with this.
  readUnifiedData(reader);
  version_ = reader.readInt();
  int64_t signed_size = reader.readLong();
  std::memcpy(&body_size_, &signed_size, sizeof(uint64_t));
}

void HazelcastHeaderEntry::writeUnifiedData(ObjectDataOutput& writer) const {
  writer.writeInt(header_map_->size());
  header_map_->iterate(
      [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
        ObjectDataOutput* writer = static_cast<ObjectDataOutput*>(context);
        absl::string_view key_view = header.key().getStringView();
        absl::string_view val_view = header.value().getStringView();
        std::vector<char> key_vector(key_view.begin(), key_view.end());
        std::vector<char> val_vector(val_view.begin(), val_view.end());
        writer->writeCharArray(&key_vector);
        writer->writeCharArray(&val_vector);
        return Http::HeaderMap::Iterate::Continue;
      },
      &writer);
  std::string serialized;
  variant_key_.SerializeToString(&serialized);
  writer.writeUTF(&serialized);
}

void HazelcastHeaderEntry::readUnifiedData(ObjectDataInput& reader) {
  int headers_size = reader.readInt();
  header_map_ = std::make_unique<Http::ResponseHeaderMapImpl>();
  for (int i = 0; i < headers_size; i++) {
    std::vector<char> key_vector = *reader.readCharArray();
    std::vector<char> val_vector = *reader.readCharArray();
    Http::HeaderString key, val;
    key.append(key_vector.data(), key_vector.size());
    val.append(val_vector.data(), val_vector.size());
    header_map_->addViaMove(std::move(key), std::move(val));
  }
  std::string serialized = *reader.readUTF();
  variant_key_.ParseFromString(serialized);
}

HazelcastHeaderEntry::HazelcastHeaderEntry() = default;

HazelcastHeaderEntry::HazelcastHeaderEntry(Http::ResponseHeaderMapPtr&& header_map, Key&& key,
                                           uint64_t body_size, int32_t version)
    : header_map_(std::move(header_map)), variant_key_(std::move(key)), body_size_(body_size),
      version_(version) {}

HazelcastHeaderEntry::HazelcastHeaderEntry(const HazelcastHeaderEntry& other) {
  body_size_ = other.body_size_;
  variant_key_ = other.variant_key_;
  header_map_ = Http::createHeaderMap<Http::ResponseHeaderMapImpl>(*other.header_map_);
  version_ = other.version_;
}

HazelcastHeaderEntry::HazelcastHeaderEntry(HazelcastHeaderEntry&& other) noexcept
    : header_map_(std::move(other.header_map_)), variant_key_(std::move(other.variant_key_)),
      body_size_(other.body_size_), version_(other.version_) {}

void HazelcastBodyEntry::writeData(ObjectDataOutput& writer) const {
  writeUnifiedData(writer);
  writer.writeInt(version_);
}

void HazelcastBodyEntry::readData(ObjectDataInput& reader) {
  readUnifiedData(reader);
  version_ = reader.readInt();
}

void HazelcastBodyEntry::writeUnifiedData(ObjectDataOutput& writer) const {
  writer.writeByteArray(&body_buffer_);
}

void HazelcastBodyEntry::readUnifiedData(ObjectDataInput& reader) {
  body_buffer_ = *reader.readByteArray();
}

HazelcastBodyEntry::HazelcastBodyEntry() = default;

HazelcastBodyEntry::HazelcastBodyEntry(std::vector<hazelcast::byte>&& buffer, int32_t version)
    : version_(version), body_buffer_(std::move(buffer)) {}

HazelcastBodyEntry::HazelcastBodyEntry(const HazelcastBodyEntry& other) {
  body_buffer_ = other.body_buffer_;
  version_ = other.version_;
}

HazelcastBodyEntry::HazelcastBodyEntry(HazelcastBodyEntry&& other) noexcept
    : version_(other.version_), body_buffer_(std::move(other.body_buffer_)) {}

void HazelcastResponseEntry::writeData(ObjectDataOutput& writer) const {
  response_header_.writeUnifiedData(writer);
  response_body_.writeUnifiedData(writer);
}

void HazelcastResponseEntry::readData(ObjectDataInput& reader) {
  response_header_.readUnifiedData(reader);
  response_body_.readUnifiedData(reader);
}

HazelcastResponseEntry::HazelcastResponseEntry() = default;

HazelcastResponseEntry::HazelcastResponseEntry(HazelcastHeaderEntry&& header,
                                               HazelcastBodyEntry&& body)
    : response_header_(std::move(header)), response_body_(std::move(body)){};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
