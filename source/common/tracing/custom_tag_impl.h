#pragma once

#include "envoy/tracing/custom_tag.h"
#include "envoy/type/tracing/v3/custom_tag.pb.h"

#include "source/common/config/metadata.h"
#include "source/common/tracing/trace_context_impl.h"

namespace Envoy {
namespace Tracing {

class CustomTagBase : public CustomTag {
public:
  explicit CustomTagBase(const std::string& tag) : tag_(tag) {}
  absl::string_view tag() const override { return tag_; }
  void applySpan(Span& span, const CustomTagContext& ctx) const override;
  void applyLog(envoy::data::accesslog::v3::AccessLogCommon& entry,
                const CustomTagContext& ctx) const override;
  virtual absl::string_view value(const CustomTagContext& ctx) const PURE;

protected:
  const std::string tag_;
};

class LiteralCustomTag : public CustomTagBase {
public:
  LiteralCustomTag(const std::string& tag,
                   const envoy::type::tracing::v3::CustomTag::Literal& literal)
      : CustomTagBase(tag), value_(literal.value()) {}
  absl::string_view value(const CustomTagContext&) const override { return value_; }

private:
  const std::string value_;
};

class EnvironmentCustomTag : public CustomTagBase {
public:
  EnvironmentCustomTag(const std::string& tag,
                       const envoy::type::tracing::v3::CustomTag::Environment& environment);
  absl::string_view value(const CustomTagContext&) const override { return final_value_; }

private:
  const std::string name_;
  const std::string default_value_;
  std::string final_value_;
};

class RequestHeaderCustomTag : public CustomTagBase {
public:
  RequestHeaderCustomTag(const std::string& tag,
                         const envoy::type::tracing::v3::CustomTag::Header& request_header);
  absl::string_view value(const CustomTagContext& ctx) const override;

private:
  const Tracing::TraceContextHandler name_;
  const std::string default_value_;
};

class MetadataCustomTag : public CustomTagBase {
public:
  MetadataCustomTag(const std::string& tag,
                    const envoy::type::tracing::v3::CustomTag::Metadata& metadata);
  void applySpan(Span& span, const CustomTagContext& ctx) const override;
  void applyLog(envoy::data::accesslog::v3::AccessLogCommon& entry,
                const CustomTagContext& ctx) const override;
  absl::string_view value(const CustomTagContext&) const override { return default_value_; }
  const envoy::config::core::v3::Metadata* metadata(const CustomTagContext& ctx) const;

protected:
  absl::optional<std::string>
  metadataToString(const envoy::config::core::v3::Metadata* metadata) const;

  const envoy::type::metadata::v3::MetadataKind::KindCase kind_;
  const Envoy::Config::MetadataKey metadata_key_;
  const std::string default_value_;
};

class CustomTagUtility {
public:
  /**
   * Create a custom tag according to the configuration.
   * @param tag a tracing custom tag configuration.
   */
  static CustomTagConstSharedPtr createCustomTag(const envoy::type::tracing::v3::CustomTag& tag);
};

} // namespace Tracing
} // namespace Envoy
