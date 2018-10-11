#include "extensions/tracers/zipkin/span_context_extractor.h"

#include "common/common/assert.h"
#include "common/common/utility.h"

#include "extensions/tracers/zipkin/span_context.h"
#include "extensions/tracers/zipkin/zipkin_core_constants.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {
constexpr int FormatMaxLength = 32 + 1 + 16 + 3 + 16; // traceid128-spanid-1-parentid
bool validSamplingFlags(char c) {
  if (c == '1' || c == '0' || c == 'd')
    return true;
  return false;
}

bool getSamplingFlags(char c, const Tracing::Decision tracing_decision) {
  if (validSamplingFlags(c)) {
    return c == '0' ? false : true;
  } else {
    return tracing_decision.traced;
  }
}

} // namespace

SpanContextExtractor::SpanContextExtractor(Http::HeaderMap& request_headers)
    : request_headers_(request_headers) {}

SpanContextExtractor::~SpanContextExtractor() {}

bool SpanContextExtractor::extractSampled(const Tracing::Decision tracing_decision) {
  bool sampled(false);
  if (request_headers_.B3()) {
    absl::string_view b3 = request_headers_.B3()->value().getStringView();
    if (b3.length() == 1) { // possibly sampling falgs
      return getSamplingFlags(b3[0], tracing_decision);
    }

    if (b3.length() >= (32 + 1 + 16 + 2) && b3[32] == '-' && b3[49] == '-') {
      return getSamplingFlags(b3[50], tracing_decision);
    }

    if (b3.length() >= (16 + 1 + 16 + 2) && b3[16] == '-' && b3[33] == '-') {
      return getSamplingFlags(b3[34], tracing_decision);
    }

    return tracing_decision.traced;
  }

  if (!request_headers_.XB3Sampled()) {
    return tracing_decision.traced;
  }
  // Checking if sampled flag has been specified. Also checking for 'true' value, as some old
  // zipkin tracers may still use that value, although should be 0 or 1.
  absl::string_view xb3_sampled = request_headers_.XB3Sampled()->value().getStringView();
  sampled = xb3_sampled == ZipkinCoreConstants::get().SAMPLED || xb3_sampled == "true";
  return sampled;
}

std::pair<SpanContext, bool> SpanContextExtractor::extractSpanContext(bool is_sampled) {
  if (request_headers_.B3()) {
    return extractSpanContextFromB3SingleFormat(is_sampled);
  }
  uint64_t trace_id(0);
  uint64_t trace_id_high(0);
  uint64_t span_id(0);
  uint64_t parent_id(0);

  if (request_headers_.XB3TraceId() && request_headers_.XB3SpanId()) {
    // Extract trace id - which can either be 128 or 64 bit. For 128 bit,
    // it needs to be divided into two 64 bit numbers (high and low).
    const std::string tid = request_headers_.XB3TraceId()->value().c_str();
    if (request_headers_.XB3TraceId()->value().size() == 32) {
      const std::string high_tid = tid.substr(0, 16);
      const std::string low_tid = tid.substr(16, 16);
      if (!StringUtil::atoul(high_tid.c_str(), trace_id_high, 16) ||
          !StringUtil::atoul(low_tid.c_str(), trace_id, 16)) {
        throw ExtractorException(
            fmt::format("Invalid traceid_high {} or tracid {}", high_tid.c_str(), low_tid.c_str()));
      }
    } else if (!StringUtil::atoul(tid.c_str(), trace_id, 16)) {
      throw ExtractorException(fmt::format("Invalid trace_id {}", tid.c_str()));
    }

    const std::string spid = request_headers_.XB3SpanId()->value().c_str();
    if (!StringUtil::atoul(spid.c_str(), span_id, 16)) {
      throw ExtractorException(fmt::format("Invalid span id {}", spid.c_str()));
    }

    if (request_headers_.XB3ParentSpanId()) {
      const std::string pspid = request_headers_.XB3ParentSpanId()->value().c_str();
      if (!StringUtil::atoul(pspid.c_str(), parent_id, 16)) {
        throw ExtractorException(fmt::format("Invalid parent span id {}", pspid.c_str()));
      }
    }
  } else {
    return std::pair<SpanContext, bool>(SpanContext(), false);
  }

  return std::pair<SpanContext, bool>(
      SpanContext(trace_id_high, trace_id, span_id, parent_id, is_sampled), true);
}

std::pair<SpanContext, bool>
SpanContextExtractor::extractSpanContextFromB3SingleFormat(bool is_sampled) {
  ASSERT(request_headers_.B3());
  const std::string b3 = request_headers_.B3()->value().c_str();
  if (!b3.length()) {
    throw ExtractorException("Invalid input: empty");
  }

  if (b3.length() == 1) { // possibly sampling falgs
    if (validSamplingFlags(b3[0])) {
      return std::pair<SpanContext, bool>(SpanContext(), false);
    }
    throw ExtractorException(fmt::format("Invalid input: invalid sampling flag {}", b3[0]));
  }

  if (b3.length() < 16 + 1 + 16 /* traceid64-spanid */) {
    throw ExtractorException("Invalid input: truncated");
  } else if (b3.length() > FormatMaxLength) {
    throw ExtractorException("Invalid input: too long");
  }

  uint64_t trace_id(0);
  uint64_t trace_id_high(0);
  uint64_t span_id(0);
  uint64_t parent_id(0);

  uint64_t pos = 0;

  const std::string trace_id_str = b3.substr(pos, 16);
  if (b3[pos + 32] == '-') {
    if (!StringUtil::atoul(trace_id_str.c_str(), trace_id_high, 16)) {
      throw ExtractorException(
          fmt::format("Invalid input: invalid trace id high {}", trace_id_str.c_str()));
    }
    pos += 16;
    const std::string trace_id_low_str = b3.substr(pos, 16);
    if (!StringUtil::atoul(trace_id_low_str.c_str(), trace_id, 16)) {
      throw ExtractorException(
          fmt::format("Invalid input: invalid trace id {}", trace_id_low_str.c_str()));
    }
  } else {
    if (!StringUtil::atoul(trace_id_str.c_str(), trace_id, 16)) {
      throw ExtractorException(
          fmt::format("Invalid input: invalid trace id {}", trace_id_str.c_str()));
    }
  }

  pos += 16; // traceId ended
  if (!(b3[pos++] == '-')) {
    throw ExtractorException("Invalid input: not exists span id");
  }

  const std::string span_id_str = b3.substr(pos, 16);
  if (!StringUtil::atoul(span_id_str.c_str(), span_id, 16)) {
    throw ExtractorException(fmt::format("Invalid input: invalid span id {}", span_id_str.c_str()));
  }
  pos += 16; // spanId ended

  if (b3.length() > pos) {
    // If we are at this point, we have more than just traceId-spanId.
    // If the sampling field is present, we'll have a delimiter 2 characters from now. Ex "-1"
    // If it is absent, but a parent ID is (which is strange), we'll have at least 17 characters.
    // Therefore, if we have less than two characters, the input is truncated.
    if (b3.length() == (pos + 1)) {
      throw ExtractorException("Invalid input: truncated");
    }

    if (!(b3[pos++] == '-')) {
      throw ExtractorException("Invalid input: not exists sampling field");
    }

    // If our position is at the end of the string, or another delimiter is one character past our
    // position, try to read sampled status.
    if (b3.length() == pos + 1 || ((b3.length() >= pos + 2) && (b3[pos + 1] == '-'))) {
      if (!validSamplingFlags(b3[pos])) {
        throw ExtractorException(fmt::format("Invalid input: invalid sampling flag {}", b3[pos]));
      }
      pos++; // consume the sampled status
    }

    if (b3.length() > pos) {
      // If we are at this point, we should have a parent ID, encoded as "-[0-9a-f]{16}"
      if (b3.length() != pos + 17) {
        throw ExtractorException("Invalid input: truncated");
      }

      if (!(b3[pos++] == '-')) {
        throw ExtractorException("Invalid input: not exists parent id");
      }

      const std::string parent_id_str = b3.substr(pos, b3.length() - pos);
      if (!StringUtil::atoul(parent_id_str.c_str(), parent_id, 16)) {
        throw ExtractorException(
            fmt::format("Invalid input: invalid parent id {}", parent_id_str.c_str()));
      }
    }
  }

  return std::pair<SpanContext, bool>(
      SpanContext(trace_id_high, trace_id, span_id, parent_id, is_sampled), true);
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy