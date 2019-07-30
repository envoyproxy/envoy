#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace HttpInspector {

/**
 * Class including extended methods.
 */
class ExtendedHeaderValues {
public:
  struct {
    const std::string Acl{"ACL"};
    const std::string Baseline_Control{"BASELINE-CONTROL"};
    const std::string Bind{"BIND"};
    const std::string Checkin{"CHECKIN"};
    const std::string Checkout{"CHECKOUT"};
    const std::string Connect{"CONNECT"};
    const std::string Copy{"COPY"};
    const std::string Delete{"DELETE"};
    const std::string Get{"GET"};
    const std::string Head{"HEAD"};
    const std::string Label{"LABEL"};
    const std::string Link{"LINK"};
    const std::string Lock{"LOCK"};
    const std::string Merge{"Merge"};
    const std::string Mkactivity{"MKACTIVITY"};
    const std::string Mkcalendar{"MKCALENDAR"};
    const std::string Mkcol{"MKCOL"};
    const std::string Mkredirectref{"MKREDIRECTREF"};
    const std::string Mkworkspace{"MKWORKSPACE"};
    const std::string Move{"MOVE"};
    const std::string Options{"OPTIONS"};
    const std::string Orderpatch{"ORDERPATCH"};
    const std::string Patch{"PATCH"};
    const std::string Post{"POST"};
    const std::string Pri{"PRI"};
    const std::string Proppatch{"PROPPATCH"};
    const std::string Purge{"PURGE"};
    const std::string Put{"PUT"};
    const std::string Rebind{"REBIND"};
    const std::string Report{"REPORT"};
    const std::string Search{"SEARCH"};
    const std::string Trace{"TRACE"};
    const std::string Unbind{"UNBIND"};
    const std::string Uncheckout{"UNCHECKOUT"};
    const std::string Unlink{"UNLINK"};
    const std::string Unlock{"UNLOCK"};
    const std::string Update{"UPDATE"};
    const std::string Updateredirectref{"UPDATEREDIRECTREF"};
    const std::string Version_Control{"VERSION-CONTROL"};
  } MethodValues;
};

using ExtendedHeader = ConstSingleton<ExtendedHeaderValues>;

} // namespace HttpInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
