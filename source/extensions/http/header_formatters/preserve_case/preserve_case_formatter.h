#pragma once

#include "envoy/http/header_formatter.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderFormatters {
namespace PreserveCase {

class PreserveCaseHeaderFormatter : public Envoy::Http::StatefulHeaderKeyFormatter {
public:
  // Envoy::Http::StatefulHeaderKeyFormatter
  std::string format(absl::string_view key) const override;
  void processKey(absl::string_view key) override;

private:
  StringUtil::CaseUnorderedSet original_header_keys_;
};

} // namespace PreserveCase
} // namespace HeaderFormatters
} // namespace Http
} // namespace Extensions
} // namespace Envoy
