#pragma once

#include <string>
#include <vector>

#include "envoy/common/hashable.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/common/hash.h"
#include "source/common/common/random_generator.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace StreamInfo {

/**
 * A FilterState object holding a version 4 (random) UUID in the binary form. The UUID is
 * generated from the cryptographically secure random number generator of the SSL library.
 *
 * When shared with the upstream connection, the full 128-bit value is appended to the upstream
 * connection pool key, which guarantees that the stream does not share the upstream connection
 * with any other stream that does not carry the same object.
 */
class UuidObject : public FilterState::Object, public Hashable {
public:
  explicit UuidObject(const Random::RandomUtility::UuidBytes& uuid) : uuid_(uuid) {}

  /**
   * @return a new object with a freshly generated random UUID.
   */
  static std::unique_ptr<UuidObject> random() {
    return std::make_unique<UuidObject>(Random::RandomUtility::uuidBytes());
  }

  /**
   * @return the binary form of the UUID.
   */
  const Random::RandomUtility::UuidBytes& bytes() const { return uuid_; }

  // FilterState::Object
  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<Protobuf::StringValue>();
    message->set_value(Random::RandomUtility::uuidToString(uuid_));
    return message;
  }
  std::optional<std::string> serializeAsString() const override {
    return Random::RandomUtility::uuidToString(uuid_);
  }

  // Hashable
  // The 64-bit hash is only suitable for the non security sensitive uses, e.g. load balancing.
  std::optional<uint64_t> hash() const override {
    return HashUtil::xxHash64(
        absl::string_view(reinterpret_cast<const char*>(uuid_.data()), uuid_.size()));
  }
  // The pool key contains the full binary UUID rather than its 64-bit hash.
  void hashKey(std::vector<uint8_t>& key) const override {
    key.insert(key.end(), uuid_.begin(), uuid_.end());
  }

private:
  const Random::RandomUtility::UuidBytes uuid_;
};

} // namespace StreamInfo
} // namespace Envoy
