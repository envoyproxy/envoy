#pragma once

#include <memory>
#include <optional>
#include <string>

#include "envoy/stream_info/filter_state.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

constexpr absl::string_view LocalReplyFilterStateKey =
    "envoy.filters.network.http_connection_manager.local_reply_owner";

/**
 * Filter state object recording which HTTP filter configuration initiated a local reply.
 * Defined in its own leaf header so StreamInfo::FilterStateImpl can validate
 * FilterStateIndex::LocalReplyOwner without depending on filter_manager_lib.
 */
class LocalReplyOwnerObject : public StreamInfo::FilterState::Object {
public:
  // The filter_config_name is expected to outlive the LocalReplyOwnerObject, as it typically
  // comes from the filter configuration.
  LocalReplyOwnerObject(absl::string_view filter_config_name)
      : filter_config_name_(filter_config_name) {}

  ProtobufTypes::MessagePtr serializeAsProto() const override {
    auto message = std::make_unique<Protobuf::StringValue>();
    message->set_value(filter_config_name_);
    return message;
  }

  std::optional<std::string> serializeAsString() const override {
    return std::string(filter_config_name_);
  }

private:
  const absl::string_view filter_config_name_;
};

} // namespace Http
} // namespace Envoy
