#pragma once

#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>

#include <string>

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
  Http::RequestHeaderMap& headers_;

public:
  explicit RequestHeaderWriter(Http::RequestHeaderMap& headers);

  void set(datadog::tracing::StringView key, datadog::tracing::StringView value) override;
};

class ResponseHeaderReader : public datadog::tracing::DictReader {
  const Http::ResponseHeaderMap& headers_;
  mutable std::string buffer_;

public:
  explicit ResponseHeaderReader(const Http::ResponseHeaderMap& headers);

  datadog::tracing::Optional<datadog::tracing::StringView>
  lookup(datadog::tracing::StringView key) const override;

  void visit(const std::function<void(datadog::tracing::StringView key,
                                      datadog::tracing::StringView value)>& visitor) const override;
};

class TraceContextReader : public datadog::tracing::DictReader {
  const Tracing::TraceContext& context_;

public:
  explicit TraceContextReader(const Tracing::TraceContext& context);

  datadog::tracing::Optional<datadog::tracing::StringView>
  lookup(datadog::tracing::StringView key) const override;

  void visit(const std::function<void(datadog::tracing::StringView key,
                                      datadog::tracing::StringView value)>& visitor) const override;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
