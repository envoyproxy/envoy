.. _start_quick_start_dynamic_envoy_gateway:

Configuration: Dynamic from Envoy Gateway Control Plane
=======================================================

Part of the Envoy project, `Envoy Gateway <https://gateway.envoyproxy.io/docs/>`_  is the control plane for dynamically managing Envoy Proxy as a gateway. Making configuring Envoy Proxy easier through simple control plane APIs.
Kubernetes `Gateway API <https://gateway-api.sigs.k8s.io/>`_ resources and Envoy Gateway extensions are used to dynamically provision and configure the managed Envoy Proxies.

.. image:: /_static/envoy-gateway-overview.svg

.. note::

   For latest up to date Quickstart installation documentation visit the Envoy Gateway docs site on  `gateway.envoyproxy.io/docs/ <https://gateway.envoyproxy.io/docs/>`_.


What to explore next?
---------------------

In this quickstart, you have:
* Installed Envoy Gateway
* Deployed a backend service, and a gateway
* Configured the gateway using Kubernetes Gateway API resources [Gateway](https://gateway-api.sigs.k8s.io/api-types/gateway/) and [HttpRoute](https://gateway-api.sigs.k8s.io/api-types/httproute/) to direct incoming requests over HTTP to the backend service.

Here is a suggested list of follow-on tasks to guide you in your exploration of Envoy Gateway:

* `HTTP Routing <https://gateway.envoyproxy.io/docs/tasks/traffic/http-routing>`_ 
* `Traffic Splitting <https://gateway.envoyproxy.io/docs/tasks/traffic/http-traffic-splitting>`_ 
* `Secure Gateways <https://gateway.envoyproxy.io/docs/tasks/security/secure-gateways/>`_ 
* `Global Rate Limit <https://gateway.envoyproxy.io/docs/tasks/traffic/global-rate-limit/>`_ 
* `gRPC Routing <https://gateway.envoyproxy.io/docs/tasks/traffic/grpc-routing/>`_ 

Find more introductions for configuring Envoy Proxy using the Envoy Gateway control plane under `Tasks <https://gateway.envoyproxy.io/docs/tasks/>`_ on the Envoy Gateway docs.


Get Help
---------

Join ``#gateway-users`` on Envoy Slack, get an invite to Envoy Slack `here <https://communityinviter.com/apps/envoyproxy/envoy>`_.