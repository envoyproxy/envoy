#pragma once

#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>

#include <string>

#include "source/extensions/tracers/datadog/dd.h"

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

class RequestHeaderWriter : public dd::DictWriter {
  Http::RequestHeaderMap* headers_;

public:
  explicit RequestHeaderWriter(Http::RequestHeaderMap& headers);

  void set(dd::StringView key, dd::StringView value) override;
};

class ResponseHeaderReader : public dd::DictReader {
  const Http::ResponseHeaderMap* headers_;
  mutable std::string buffer_;

public:
  explicit ResponseHeaderReader(const Http::ResponseHeaderMap& headers);

  dd::Optional<dd::StringView> lookup(dd::StringView key) const override;

  void visit(
      const std::function<void(dd::StringView key, dd::StringView value)>& visitor) const override;
};

class TraceContextReader : public dd::DictReader {
  const Tracing::TraceContext* context_;

public:
  explicit TraceContextReader(const Tracing::TraceContext& context);

  dd::Optional<dd::StringView> lookup(dd::StringView key) const override;

  void visit(
      const std::function<void(dd::StringView key, dd::StringView value)>& visitor) const override;
};

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
