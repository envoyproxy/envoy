#include "source/extensions/filters/common/expr/custom_cel/extended_request/utility/utility.h"

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

using google::api::expr::runtime::CelList;
using google::api::expr::runtime::CelMap;
using google::api::expr::runtime::CelValue;
using google::api::expr::runtime::ContainerBackedListImpl;

CelList* appendList(Protobuf::Arena& arena, const CelList* list1, const CelList* list2) {
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

CelValue createCelMap(Protobuf::Arena& arena,
                      std::vector<std::pair<CelValue, CelValue>> key_value_pairs) {
  // create ContainerBackedMapImpl from vector of key value pairs
  std::unique_ptr<CelMap> map_unique_ptr =
      CreateContainerBackedMap(absl::Span<std::pair<CelValue, CelValue>>(key_value_pairs)).value();

  // transfer ownership of map from unique_ptr to arena
  CelMap* map_raw_ptr = map_unique_ptr.release();
  arena.Own(map_raw_ptr);
  return CelValue::CreateMap(map_raw_ptr);
}

} // namespace Utility
} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
