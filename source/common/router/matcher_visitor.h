#pragma once

#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Router {

class RouteActionValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view type_url) override;
};

} // namespace Router
} // namespace Envoy
