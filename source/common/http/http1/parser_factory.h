#pragma once

#include "common/http/http1/parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {

/**
 * A temporary factory class to allow switching between constructing a parser using the legacy
 * http-parser library or llhttp.
 */
class ParserFactory {
public:
  /**
   * Creates a new parser implementation.
   */
  static ParserPtr create(MessageType type, void* data);

  /**
   * @return whether the factory is configured to return the legacy HTTP parser.
   */
  static bool usesLegacyParser();

  /**
   * Sets whether to construct the legacy HTTP parser or newer llhttp parser.
   */
  static void useLegacy(bool use_legacy_parser);

private:
  static bool use_legacy_parser_;
};

} // namespace Http1
} // namespace Http
} // namespace Envoy
