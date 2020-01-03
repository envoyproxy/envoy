#pragma once

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

class ApiTypeOracle {
public:
  /**
   * Based on a given message, determine if there exists an earlier version of
   * this message. If so, return the descriptor for the earlier
   * message, to support upgrading via VersionConverter::upgrade().
   *
   * @param message protobuf message.
   * @return const Protobuf::Descriptor* descriptor for earlier message version
   *         corresponding to message, if any, otherwise nullptr.
   */
  static const Protobuf::Descriptor* getEarlierVersionDescriptor(const Protobuf::Message& message);
};

} // namespace Config
} // namespace Envoy
