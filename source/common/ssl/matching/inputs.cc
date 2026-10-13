#include "source/common/ssl/matching/inputs.h"

#include "envoy/http/filter.h"
#include "envoy/network/filter.h"

namespace Envoy {
namespace Ssl {
namespace Matching {

using UriSanInputFactory = UriSanInputBaseFactory<Network::MatchingData>;
using DnsSanInputFactory = DnsSanInputBaseFactory<Network::MatchingData>;
using SubjectInputFactory = SubjectInputBaseFactory<Network::MatchingData>;

REGISTER_FACTORY(UriSanInputFactory, Matcher::DataInputFactory<Network::MatchingData>);
REGISTER_FACTORY(DnsSanInputFactory, Matcher::DataInputFactory<Network::MatchingData>);
REGISTER_FACTORY(SubjectInputFactory, Matcher::DataInputFactory<Network::MatchingData>);

using HttpUriSanInputFactory = UriSanInputBaseFactory<Http::HttpMatchingData>;
using HttpDnsSanInputFactory = DnsSanInputBaseFactory<Http::HttpMatchingData>;
using HttpSubjectInputFactory = SubjectInputBaseFactory<Http::HttpMatchingData>;

REGISTER_FACTORY(HttpUriSanInputFactory, Matcher::DataInputFactory<Http::HttpMatchingData>);
REGISTER_FACTORY(HttpDnsSanInputFactory, Matcher::DataInputFactory<Http::HttpMatchingData>);
REGISTER_FACTORY(HttpSubjectInputFactory, Matcher::DataInputFactory<Http::HttpMatchingData>);

} // namespace Matching
} // namespace Ssl
} // namespace Envoy
