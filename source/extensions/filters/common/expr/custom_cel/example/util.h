#pragma once

#include "source/common/protobuf/protobuf.h"

#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_list_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_CEL {
namespace Example {

using google::api::expr::runtime::CelValue;
using google::api::expr::runtime::ContainerBackedListImpl;

class Utility {
public:
  Utility() = default;

  // appendList: Appends one ContainerBackedListImpl to another.
  // Memory is allocated for it on the arena.
  static ContainerBackedListImpl* appendList(Protobuf::Arena& arena,
                                             const ContainerBackedListImpl* list1,
                                             const ContainerBackedListImpl* list2) {
    std::vector<CelValue> keys;
    keys.reserve(list1->size() + list2->size());
    for (int i = 0; i < list1->size(); ++i) {
      keys.push_back(((*list1)[i]));
    }
    for (int i = 0; i < list2->size(); i++) {
      keys.push_back(((*list2)[i]));
    }
    return Protobuf::Arena::Create<ContainerBackedListImpl>(&arena, keys);
  }

  ~Utility() = default;
};

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
