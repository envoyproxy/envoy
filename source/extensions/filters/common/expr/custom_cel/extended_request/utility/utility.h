#pragma once

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
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

// createCelMap: given an absl map, create a CelMap on the arena
google::api::expr::runtime::CelValue
createCelMap(Protobuf::Arena& arena, absl::flat_hash_map<std::string, std::string> map);

// createCelMap: given an std map, create a CelMap on the arena
google::api::expr::runtime::CelValue createCelMap(Protobuf::Arena& arena,
                                                  std::map<std::string, std::string> map);

} // namespace Utility
} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
