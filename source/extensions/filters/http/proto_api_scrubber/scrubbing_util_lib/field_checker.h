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

/**
 * FieldChecker class encapsulates the scrubbing logic of `ProtoApiScrubber` filter.
 * This `FieldChecker` would be integrated with `proto_processing_lib::proto_scrubber` library for
 * protobuf payload scrubbing. The `CheckField()` method declared in the parent class
 * `FieldCheckerInterface` and defined in this class is called by the
 * `proto_processing_lib::proto_scrubber` library for each field of the protobuf payload to decide
 * whether to preserve, remove or traverse it further.
 */
class FieldChecker : public FieldCheckerInterface {
public:
  FieldChecker() {}

  // This type is neither copyable nor movable.
  FieldChecker(const FieldChecker&) = delete;
  FieldChecker& operator=(const FieldChecker&) = delete;
  ~FieldChecker() override {}

  /**
   * Returns whether the `field` should be included (kInclude), excluded (kExclude)
   * or traversed further (kPartial).
   */
  FieldCheckResults CheckField(const std::vector<std::string>& path,
                               const Protobuf::Field* field) const override;

  /**
   * Returns false as it currently doesn't support `google.protobuf.Any` type.
   */
  bool SupportAny() const override { return false; }

  /**
   * Returns whether the `type` should be included (kInclude), excluded (kExclude)
   * or traversed further (kPartial).
   */
  FieldCheckResults CheckType(const Protobuf::Type* type) const override;

  FieldFilters FilterName() const override { return FieldFilters::FieldMaskFilter; }
};

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
