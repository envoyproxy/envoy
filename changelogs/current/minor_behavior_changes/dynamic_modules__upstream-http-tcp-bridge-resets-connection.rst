A reset of a request on the upstream HTTP TCP bridge (``envoy.upstreams.http.dynamic_modules``) now
tries to reset its upstream connection, which only Linux supports, instead of ending it with a FIN,
so the upstream does not read a cut request as a whole one. The bridge also drops upstream data a
module sends after it ended its upstream data.
