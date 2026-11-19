#pragma once

#include "envoy/config/subscription.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Config {

template <typename Current> class OpaqueResourceDecoderImpl : public Config::OpaqueResourceDecoder {
public:
  OpaqueResourceDecoderImpl(ProtobufMessage::ValidationVisitor& validation_visitor,
                            absl::string_view name_field)
      : validation_visitor_(validation_visitor), name_field_(name_field),
        arena_options_(defaultArenaOptions()) {}

  // Config::OpaqueResourceDecoder
  Protobuf::Arena::UniquePtr<Protobuf::Message> decodeResource(const Protobuf::Any& resource,
                                                               Protobuf::Arena& arena) override {
    auto typed_message = Protobuf::Arena::MakeUnique<Current>(&arena);
    // If the Any is a synthetic empty message (e.g. because the resource field was not set in
    // Resource, this might be empty, so we shouldn't decode.
    if (!resource.type_url().empty()) {
      MessageUtil::anyConvertAndValidate<Current>(resource, *typed_message, validation_visitor_);
    }
    return typed_message;
  }

  std::string resourceName(const Protobuf::Message& resource) override {
    return MessageUtil::getStringField(resource, name_field_);
  }

  const Protobuf::ArenaOptions& arenaOptions() const override { return arena_options_; }

private:
  // Decoded resources are ephemeral; these arena options are optimized to reduce memory allocations
  // and memory fragmentation when decoding wire-encoded opaque resources rather than space
  // efficiency.
  static Protobuf::ArenaOptions defaultArenaOptions() {
    Protobuf::ArenaOptions options;
    // Skips the default small initial arena block sizes.
    options.start_block_size = 8 * 1024;
    // Allows the arena block size to grow beyond the default for truly large resources.
    options.max_block_size = 1024 * 1024;
    return options;
  }

  ProtobufMessage::ValidationVisitor& validation_visitor_;
  const std::string name_field_;
  const Protobuf::ArenaOptions arena_options_;
};

} // namespace Config
} // namespace Envoy
