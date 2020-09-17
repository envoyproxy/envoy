#pragma once

#include "common/protobuf/protobuf.h"

#include "absl/types/optional.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

class ApiTypeOracle {
public:
  /**
   * Based on a given message, determine if there exists an earlier version of
   * this message. If so, return the descriptor for the earlier
   * message, to support upgrading via VersionConverter::upgrade().
   *
   * @param message_type protobuf message type
   * @return const Protobuf::Descriptor* descriptor for earlier message version
   *         corresponding to message, if any, otherwise nullptr.
   */
  static const Protobuf::Descriptor* getEarlierVersionDescriptor(const std::string& message_type);

  static const absl::optional<std::string>
  getEarlierVersionMessageTypeName(const std::string& message_type);

  static const absl::string_view typeUrlToDescriptorFullName(absl::string_view type_url);

  static const std::string descriptorFullNameToTypeUrl(std::string& type);

  static const absl::optional<std::string> getEarlierTypeUrl(const std::string& type_url);
};

} // namespace Config
} // namespace Envoy
