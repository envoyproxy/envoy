#pragma once

/*
 * This file contains implementations of the datadog::tracing::DictReader and
 * datadog::tracing::DictWriter interfaces.
 *
 * The Datadog core tracing library, dd-trace-cpp, uses these interfaces
 * anywhere it needs to read from or write to mappings of string, such as when
 * extracting trace context, injecting trace context, reading HTTP response
 * headers, or writing HTTP request headers.
 */

#include <string>

#include "datadog/dict_reader.h"
#include "datadog/dict_writer.h"

namespace Envoy {
namespace Tracing {
class TraceContext;
} // namespace Tracing
namespace Http {
class RequestHeaderMap;
class ResponseHeaderMap;
} // namespace Http
namespace Extensions {
namespace Tracers {
namespace Datadog {

class RequestHeaderWriter : public datadog::tracing::DictWriter {
public:
  explicit RequestHeaderWriter(Http::RequestHeaderMap& headers);

  void set(datadog::tracing::StringView key, datadog::tracing::StringView value) override;

private:
  Http::RequestHeaderMap& headers_;
};

class ResponseHeaderReader : public datadog::tracing::DictReader {
public:
  explicit ResponseHeaderReader(const Http::ResponseHeaderMap& headers);

  datadog::tracing::Optional<datadog::tracing::StringView>
  lookup(datadog::tracing::StringView key) const override;

  void visit(const std::function<void(datadog::tracing::StringView key,
                                      datadog::tracing::StringView value)>& visitor) const override;

private:
  const Http::ResponseHeaderMap& headers_;
  mutable std::string buffer_;
};

class TraceContextReader : public datadog::tracing::DictReader {
public:
  explicit TraceContextReader(const Tracing::TraceContext& context);

  datadog::tracing::Optional<datadog::tracing::StringView>
  lookup(datadog::tracing::StringView key) const override;

  void visit(const std::function<void(datadog::tracing::StringView key,
                                      datadog::tracing::StringView value)>& visitor) const override;

private:
  const Tracing::TraceContext& context_;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
