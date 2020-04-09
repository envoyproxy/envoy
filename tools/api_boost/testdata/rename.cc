#include "envoy/api/v2/route/route.pb.h"

void test() {
  envoy::api::v2::route::RouteAction route_action;
  route_action.host_rewrite();
  route_action.set_host_rewrite("blah");
}
