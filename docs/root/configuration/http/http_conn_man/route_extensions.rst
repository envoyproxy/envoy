.. _config_http_conn_man_route_extensions:

Route extensions
================

Route matching resolves at most one :ref:`route <envoy_v3_api_msg_config.route.v3.Route>` for a
request. Route extensions run after that, and are given the resolved route so that they can
customize or monitor it. The route that comes out of them is the route Envoy uses for the request.

A route extension acts on the route, not on the request: it does not modify the request attributes
(e.g., headers, path) directly. Everything it wants to change about the way the request is
proxied - the cluster, the timeout, the retry policy, the header transforms - it changes by
returning a different route.

This makes route extensions a good fit for behaviors that would otherwise need a custom HTTP filter
to reach into routing, such as picking a cluster from data that is only available at request time,
overriding a timeout for a subset of traffic, or exporting information about the chosen route.

Configuration
-------------

Extensions are configured with
:ref:`TypedExtensionConfig <envoy_v3_api_msg_config.core.v3.TypedExtensionConfig>` at three levels
of the route configuration, and the levels run in this order:

#. :ref:`RouteConfiguration.route_extensions
   <envoy_v3_api_field_config.route.v3.RouteConfiguration.route_extensions>`
#. :ref:`VirtualHost.route_extensions
   <envoy_v3_api_field_config.route.v3.VirtualHost.route_extensions>` of the resolved virtual host
#. :ref:`Route.route_extensions <envoy_v3_api_field_config.route.v3.Route.route_extensions>` of the
   resolved route

Within a level the extensions run in the order they are configured. The route that route matching
resolved is the input of the first extension, the output of each extension is the input of the
next, and the output of the last one is the final route:

.. code-block:: text

  route matching  ->  route config level  ->  virtual host level  ->  route level  ->  final route

In the following configuration, a request that reaches ``/api`` runs three extensions - ``audit``,
then ``canary``, then ``slow_timeout`` - while a request that reaches ``/`` runs only ``audit`` and
``canary``:

.. code-block:: yaml

  name: local_route
  route_extensions:
  - name: audit
    typed_config:
      # ... audit extension config ...
  virtual_hosts:
  - name: local_service
    domains: ["*"]
    route_extensions:
    - name: canary
      typed_config:
        # ... canary extension config ...
    routes:
    - match:
        prefix: "/api"
      route:
        cluster: api_service
      route_extensions:
      - name: slow_timeout
        typed_config:
          # ... slow_timeout extension config ...
    - match:
        prefix: "/"
      route:
        cluster: web_service

A level is only reached once it has been resolved. A request that matches no virtual host runs the
route configuration level alone, and a request that matches a virtual host but none of its routes
runs the route configuration and virtual host levels.

Requests with no resolved route
-------------------------------

The route configuration and virtual host levels run whether or not route matching resolved a route.
When it did not, the first extension is simply given no route, which has two consequences:

* An extension may **generate** a route for a request that matching resolved nothing for, rather
  than letting Envoy return the usual 404 response.
* An extension may **drop** the route it was given by returning null. If the route that comes
  out of the last extension is null, the request is handled as if nothing had resolved, and
  Envoy returns a 404 response.

The route level is the exception: those extensions are configured on the route itself, so they only
run when that route resolved.

Ending the chain early
----------------------

An extension may declare its result final. No further extension runs, neither the rest of its own
level nor any of the levels after it, and its result becomes the final route. This is how an
extension that has fully decided the route - a fallback route for an unresolved request, for
instance - keeps later extensions from overriding it.

Writing a route extension
-------------------------

A route extension implements the ``Envoy::Router::RouteExtension`` interface in
:repo:`envoy/router/route_extension.h`, and is registered by a factory in the
``envoy.router.route_extensions`` category. The ``onRoute()`` method takes the route produced by
the previous extension, the request headers, the stream info of the downstream request, and a
stable per-request random seed for extensions that need to make a weighted choice. It returns the
route to hand to the next extension, and whether the chain carries on.

Two constraints are worth calling out:

* A single instance is shared by all worker threads, so implementations must be thread safe and
  must not retain per-request state. Anything a request needs belongs on the route the extension
  returns.
* The request headers are read-only. Header mutations belong in the header transforms of the
  returned route, so that Envoy applies them at the right point of the request lifetime.

The usual way to implement one is to return a ``DelegatingRoute`` (see
:repo:`source/common/router/delegating_route_impl.h`) that wraps the route the extension was given
and overrides the few methods it cares about. A ``nullptr`` route, or a route with no route entry
behind it such as a redirect or a direct response, has nothing to wrap, and is typically returned
unchanged.
