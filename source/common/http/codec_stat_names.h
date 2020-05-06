#pragma once

#include "common/stats/symbol_table_impl.h"

namespace Envoy {
namespace Http {

/**
 * All stat tokens for the HTTP/1 codec.
 */
struct CodecStatNames {
  CodecStatNames(Stats::SymbolTable& symbol_table)
      : pool_(symbol_table),
        dropped_headers_with_underscores_(pool_.add("dropped_headers_with_underscores")),
        metadata_not_supported_error_(pool_.add("metadata_not_supported_error")),
        requests_rejected_with_underscores_in_headers_(pool_.add("requests_rejected_with_underscores_in_headers")),
        response_flood_(pool_.add("response_flood")),
        http1_(pool_.add("http1")) {}

  Stats::StatNamePool pool_;
  Stats::StatName dropped_headers_with_underscores_;
  Stats::StatName metadata_not_supported_error_;
  Stats::StatName requests_rejected_with_underscores_in_headers_;
  Stats::StatName response_flood_;
  Stats::StatName http1_;
};

} // namespace Http
} // namespace Envoy
