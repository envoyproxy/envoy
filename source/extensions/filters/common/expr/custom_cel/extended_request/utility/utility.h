#pragma once

#include "source/common/protobuf/protobuf.h"

#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_list_impl.h"
#include "eval/public/containers/container_backed_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {
namespace Utility {

// appendList: Appends one CelList to another.
// Memory is allocated for it on the arena.
google::api::expr::runtime::CelList* appendList(Protobuf::Arena& arena,
                                                const google::api::expr::runtime::CelList* list1,
                                                const google::api::expr::runtime::CelList* list2);

// createCelMap: Creates a CelMap from a vector of key_value_pairs.
// Memory is allocated for it on the arena.
google::api::expr::runtime::CelValue createCelMap(
    Protobuf::Arena& arena,
    std::vector<
        std::pair<google::api::expr::runtime::CelValue, google::api::expr::runtime::CelValue>>
        key_value_pairs);

} // namespace Utility
} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
