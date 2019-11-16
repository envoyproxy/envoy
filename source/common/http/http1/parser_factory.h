#pragma once

#include "common/http/http1/parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {

class ParserFactory {
public:
  /**
   * Creates a new parser implementation.
   */
  static ParserPtr create(MessageType type, void* data);

  static bool usesLegacyParser();

  static void useLegacy(bool use_legacy_parser);
private:
  static bool use_legacy_parser_;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
