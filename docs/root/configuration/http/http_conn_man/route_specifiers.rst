.. _config_http_conn_man_route_specifiers:

Route specifiers
================

Route matching resolves at most one :ref:`route <envoy_v3_api_msg_config.route.v3.Route>` for a
request. Route specifiers run after that, and are given the resolved route so that they can
customize or monitor it. The route that comes out of them is the route Envoy uses for the request.

A route specifier acts on the route, not on the request: it does not modify the request attributes
(e.g., headers, path) directly. Everything it wants to change about the way the request is
proxied - the cluster, the timeout, the retry policy, the header transforms - it changes by
returning a different route.

This makes route specifiers a good fit for behaviors that would otherwise need a custom HTTP filter
to reach into routing, such as picking a cluster from data that is only available at request time,
overriding a timeout for a subset of traffic, or exporting information about the chosen route.

Configuration
-------------

Specifiers are configured with
:ref:`TypedExtensionConfig <envoy_v3_api_msg_config.core.v3.TypedExtensionConfig>` at three levels
of the route configuration, and the levels run in this order:

#. :ref:`Route.route_specifiers <envoy_v3_api_field_config.route.v3.Route.route_specifiers>` of the
   resolved route
#. :ref:`VirtualHost.route_specifiers
   <envoy_v3_api_field_config.route.v3.VirtualHost.route_specifiers>` of the resolved virtual host
#. :ref:`RouteConfiguration.route_specifiers
   <envoy_v3_api_field_config.route.v3.RouteConfiguration.route_specifiers>`

Within a level the specifiers run in the order they are configured. The route that route matching
resolved is the input of the first specifier, the output of each specifier is the input of the
next, and the output of the last one is the final route:

.. code-block:: text

  route matching  ->  route level  ->  virtual host level  ->  route config level  ->  final route

The most specific level runs first, which leaves the last word to the specifiers that are shared
by the whole route configuration.

In the following configuration, a request that reaches ``/api`` runs three specifiers -
``slow_timeout``, then ``canary``, then ``audit`` - while a request that reaches ``/`` runs only
``canary`` and ``audit``:

.. code-block:: yaml

  name: local_route
  route_specifiers:
  - name: audit
    typed_config:
      # ... audit specifier config ...
  virtual_hosts:
  - name: local_service
    domains: ["*"]
    route_specifiers:
    - name: canary
      typed_config:
        # ... canary specifier config ...
    routes:
    - match:
        prefix: "/api"
      route:
        cluster: api_service
      route_specifiers:
      - name: slow_timeout
        typed_config:
          # ... slow_timeout specifier config ...
    - match:
        prefix: "/"
      route:
        cluster: web_service

A level only runs once it has been resolved. A request that matches a virtual host but none of its
routes runs the virtual host and route configuration levels, and a request that matches no virtual
host runs the route configuration level alone.

.. _config_http_conn_man_route_specifiers_no_route:

Requests with no resolved route
-------------------------------

The virtual host and route configuration levels run whether or not route matching resolved a route.
When it did not, the first specifier is simply given no route, which has two consequences:

* A specifier may **generate** a route for a request that matching resolved nothing for, rather
  than letting Envoy return the usual 404 response.
* A specifier may **drop** the route it was given by returning null. If the route that comes
  out of the last specifier is null, the request is handled as if nothing had resolved, and
  Envoy returns a 404 response.

The route level is the exception: those specifiers are configured on the route itself, so they only
run when that route resolved.

Ending the chain early
----------------------

A specifier may declare its result final. No further specifier runs, neither the rest of its own
level nor any of the levels after it, and its result becomes the final route. This is how an
specifier that has fully decided the route - a fallback route for an unresolved request, for
instance - keeps later specifiers from overriding it.

Continuing route matching
-------------------------

The specifiers run while the routes of the virtual host are being evaluated, each time one of them
matches the request. Besides the route, a specifier may return a match status that tells whether
the matched route is accepted, or whether route matching continues with the next route of the
virtual host. The last specifier that returns a match status decides it, and a specifier that
returns none leaves the decision to the others.

When the specifiers ask to continue, the matched route is skipped and the next route that matches
the request runs the specifiers again, with the route level taken from that route. If none of the
routes is accepted, the request has no resolved route, and the virtual host and route configuration
levels run once more as described in
:ref:`Requests with no resolved route <config_http_conn_man_route_specifiers_no_route>`. A specifier
may therefore run more than once for a single request.

Dropping the route is not the same as asking to continue. Unless the specifiers ask to continue,
what they produced is accepted, so a route that they dropped leaves the request with no route and
the remaining routes of the virtual host are not evaluated.

Writing a route specifier
-------------------------

A route specifier implements the ``Envoy::Router::RouteSpecifier`` interface in
:repo:`envoy/router/route_specifier.h`, and is registered by a factory in the
``envoy.router.route_specifiers`` category. The ``onRoute()`` method takes the route produced by
the previous specifier, the request headers, the stream info of the downstream request, and a
stable per-request random seed for specifiers that need to make a weighted choice. It returns the
route to hand to the next specifier, and whether the chain carries on.

Two constraints are worth calling out:

* A single instance is shared by all worker threads, so implementations must be thread safe and
  must not retain per-request state. Anything a request needs belongs on the route the specifier
  returns.
* The request headers are read-only. Header mutations belong in the header transforms of the
  returned route, so that Envoy applies them at the right point of the request lifetime.

The usual way to implement one is to return a ``DelegatingRoute`` or a ``DelegatingRouteEntry`` (see
:repo:`source/common/router/delegating_route_impl.h`) that wraps the route the specifier was given
and overrides the few methods it cares about. A ``nullptr`` route, or a route with no route entry
behind it such as a redirect or a direct response, has nothing to wrap, and is typically returned
unchanged.
