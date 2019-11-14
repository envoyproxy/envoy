#pragma once

#ifdef ENVOY_ENABLE_LEGACY_HTTP_PARSER
#include <http_parser.h>
#else
#include <llhttp.h>
#endif /* ENVOY_ENABLE_LEGACY_HTTP_PARSER */

/**
 * This is a temporary shim to easily allow switching between llhttp and http-parser at compile
 * time by providing a consistent interface, then adapting them to the respective implementations.
 *
 * When http-parser is ready to be removed, this shim should also disappear and the llhttp_* methods
 * moved into the codec implementation.
 */

namespace Envoy {
namespace Http {
namespace Http1 {

#ifdef ENVOY_ENABLE_LEGACY_HTTP_PARSER

using parser_type_t = enum http_parser_type; // NOLINT(readability-identifier-naming)
using parser_errno_t = enum http_errno; // NOLINT(readability-identifier-naming)
using parser_settings_t = http_parser_settings; // NOLINT(readability-identifier-naming)
using parser_t = http_parser; // NOLINT(readability-identifier-naming)
using parser_method = http_method; // NOLINT(readability-identifier-naming)

inline void parser_init(parser_t* parser, parser_type_t parser_type, parser_settings_t*) {
  http_parser_init(parser, parser_type);
}
const auto parser_execute = http_parser_execute;

inline void parser_resume(parser_t* parser) {
  http_parser_pause(parser, 0);
}

inline parser_errno_t parser_get_errno(parser_t* parser) {
  return HTTP_PARSER_ERRNO(parser);
}

const auto parser_errno_name = http_errno_name;
const auto parser_method_name = http_method_str;

#else

using parser_type_t = llhttp_type_t;  // NOLINT(readability-identifier-naming)
using parser_errno_t = llhttp_errno_t; // NOLINT(readability-identifier-naming)
using parser_settings_t = llhttp_settings_s; // NOLINT(readability-identifier-naming)
using parser_t = llhttp_t; // NOLINT(readability-identifier-naming)
using parser_method = llhttp_method; // NOLINT(readability-identifier-naming)

const auto parser_init = llhttp_init;
inline size_t parser_execute(parser_t* parser, parser_settings_t*, const char* slice, int len) {
  parser_errno_t err;
  if (slice == nullptr || len == 0) {
    err = llhttp_finish(parser);
  } else {
    err = llhttp_execute(parser, slice, len);
  }

  size_t nread = len;
  if (err != HPE_OK) {
    nread = llhttp_get_error_pos(parser) - slice;
    if (err == HPE_PAUSED_UPGRADE) {
      err = HPE_OK;
      llhttp_resume_after_upgrade(parser);
    }
  }

  return nread;
}
const auto parser_resume = llhttp_resume;
const auto parser_get_errno = llhttp_get_errno;
const auto parser_errno_name = llhttp_errno_name;
const auto parser_method_name = llhttp_method_name;

#endif /* ENVOY_ENABLE_LEGACY_HTTP_PARSER */

} // namespace Http1
} // namespace Http
} // namespace Envoy
