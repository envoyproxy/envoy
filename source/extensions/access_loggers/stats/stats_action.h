#pragma once

#include "envoy/extensions/access_loggers/stats/v3/stats.pb.h"

#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace StatsAccessLog {

struct ActionContext {};

class StatsAction {
public:
  enum class ActionType {
    DropStat,
    DropTag,
    InsertTag,
  };

  virtual ~StatsAction() = default;
  virtual ActionType type() const PURE;
};

class DropStatAction
    : public Matcher::ActionBase<envoy::extensions::access_loggers::stats::v3::DropStatAction>,
      public StatsAction {
public:
  explicit DropStatAction(const envoy::extensions::access_loggers::stats::v3::DropStatAction&) {}

  ActionType type() const override { return ActionType::DropStat; }
};

class InsertTagAction
    : public Matcher::ActionBase<envoy::extensions::access_loggers::stats::v3::InsertTagAction>,
      public StatsAction {
public:
  explicit InsertTagAction(
      const envoy::extensions::access_loggers::stats::v3::InsertTagAction& config)
      : tag_name_(config.tag_name()), tag_value_(config.tag_value()) {}

  ActionType type() const override { return ActionType::InsertTag; }

  const std::string& tagName() const { return tag_name_; }
  const std::string& tagValue() const { return tag_value_; }

private:
  const std::string tag_name_;
  const std::string tag_value_;
};

class DropTagAction
    : public Matcher::ActionBase<envoy::extensions::access_loggers::stats::v3::DropTagAction>,
      public StatsAction {
public:
  explicit DropTagAction(const envoy::extensions::access_loggers::stats::v3::DropTagAction& config)
      : target_tag_name_(config.target_tag_name()) {}

  ActionType type() const override { return ActionType::DropTag; }

  const std::string& targetTagName() const { return target_tag_name_; }

private:
  const std::string target_tag_name_;
};

} // namespace StatsAccessLog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
