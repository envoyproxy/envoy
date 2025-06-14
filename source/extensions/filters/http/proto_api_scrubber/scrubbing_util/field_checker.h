#pragma once

#include <string>
#include <vector>

#include "source/common/protobuf/protobuf.h"

#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

using proto_processing_lib::proto_scrubber::FieldCheckerInterface;
using proto_processing_lib::proto_scrubber::FieldCheckResults;
using proto_processing_lib::proto_scrubber::FieldFilters;

class FieldChecker : public FieldCheckerInterface {
public:
  FieldChecker(const Protobuf::Type*) {}

  // This type is neither copyable nor movable.
  FieldChecker(const FieldChecker&) = delete;
  FieldChecker& operator=(const FieldChecker&) = delete;
  ~FieldChecker() override {}

  FieldCheckResults CheckField(const std::vector<std::string>& path,
                               const Protobuf::Field* field) const override;

  FieldCheckResults CheckField(const std::vector<std::string>& path, const Protobuf::Field* field,
                               int field_depth, const Protobuf::Type* parent_type) const override;

  bool SupportAny() const override { return false; }

  FieldCheckResults CheckType(const Protobuf::Type* type) const override;

  // TODO: Rename this filter.
  FieldFilters FilterName() const override { return FieldFilters::FieldMaskFilter; }
};

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
