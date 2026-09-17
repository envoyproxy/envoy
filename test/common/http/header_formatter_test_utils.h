#pragma once

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/http/header_formatter.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {

// A stateful header key formatter factory config that always rejects its configuration. It covers
// the path where parseHttp1Settings() has to surface a formatter creation failure through
// creation_status instead of returning half-built settings, which is otherwise unreachable in
// tests because every in-tree formatter accepts any valid configuration.
class RejectingStatefulFormatterFactoryConfig : public StatefulHeaderKeyFormatterFactoryConfig {
public:
  static constexpr absl::string_view kName = "envoy.test.rejecting_formatter";
  static constexpr absl::string_view kError = "rejecting formatter rejected the configuration";

  std::string name() const override { return std::string(kName); }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::StringValue>();
  }

  absl::StatusOr<StatefulHeaderKeyFormatterFactorySharedPtr>
  createFactoryFromProto(const Protobuf::Message&,
                         Server::Configuration::GenericFactoryContext&) override {
    return absl::InvalidArgumentError(kError);
  }
};

// Configures the given HTTP/1 protocol options to use the formatter above. The equivalent YAML,
// for the config entry points that are exercised through it, is:
//
//   header_key_format:
//     stateful_formatter:
//       name: envoy.test.rejecting_formatter
//       typed_config:
//         "@type": type.googleapis.com/google.protobuf.StringValue
inline void
useRejectingStatefulFormatter(envoy::config::core::v3::Http1ProtocolOptions& http1_options) {
  auto* stateful_formatter =
      http1_options.mutable_header_key_format()->mutable_stateful_formatter();
  stateful_formatter->set_name(std::string(RejectingStatefulFormatterFactoryConfig::kName));
  Protobuf::StringValue formatter_config;
  std::ignore = stateful_formatter->mutable_typed_config()->PackFrom(formatter_config);
}

} // namespace Http
} // namespace Envoy
