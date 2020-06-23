.. _config_http_filters_squash:

Squash
======

Squash is an HTTP filter which enables Envoy to integrate with Squash microservices debugger.
Code: https://github.com/solo-io/squash, API Docs: https://squash.solo.io/

Overview
--------

The main use case for this filter is in a service mesh, where Envoy is deployed as a sidecar.
Once a request marked for debugging enters the mesh, the Squash Envoy filter reports its 'location'
in the cluster to the Squash server - as there is a 1-1 mapping between Envoy sidecars and
application containers, the Squash server can find and attach a debugger to the application container.
The Squash filter also holds the request until a debugger is attached (or a timeout occurs). This
enables developers (via Squash) to attach a native debugger to the container that will handle the
request, before the request arrive to the application code, without any changes to the cluster.

Configuration
-------------

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.squash.v3.Squash>`
* This filter should be configured with the name *envoy.filters.http.squash*.

How it works
------------

When the Squash filter encounters a request containing the header 'x-squash-debug' it will:

1. Delay the incoming request.
2. Contact the Squash server and request the creation of a DebugAttachment

   - On the Squash server side, Squash will attempt to attach a debugger to the application Envoy
     proxies to. On success, it changes the state of the DebugAttachment
     to attached.

3. Wait until the Squash server updates the DebugAttachment object's state to attached (or
   error state)
4. Resume the incoming request
