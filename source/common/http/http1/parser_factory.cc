#include "common/http/http1/parser_factory.h"

#include <memory>

#include "common/http/http1/legacy_http_parser.h"
#include "common/http/http1/llhttp_parser.h"

namespace Envoy {
namespace Http {
namespace Http1 {

bool ParserFactory::use_legacy_parser_ = false;

ParserPtr ParserFactory::create(MessageType type, void* data) {
  if (usesLegacyParser()) {
    return std::make_unique<LegacyHttpParserImpl>(type, data);
  }

  return std::make_unique<LlHttpParserImpl>(type, data);
}

bool ParserFactory::usesLegacyParser() { return use_legacy_parser_; }

void ParserFactory::useLegacy(bool use_legacy_parser) { use_legacy_parser_ = use_legacy_parser; }

} // namespace Http1
} // namespace Http
} // namespace Envoy
