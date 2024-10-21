.. _start_quick_start_dynamic_envoy_gateway:

Configuration: Dynamic from Envoy Gateway Control Plane
=======================================================

Part of the Envoy project, `Envoy Gateway <https://gateway.envoyproxy.io/docs/>`_  is the control plane for dynamically managing Envoy Proxy as a gateway. Making configuring Envoy Proxy easier through simple control plane APIs.
Kubernetes `Gateway API <https://gateway-api.sigs.k8s.io/>`_ resources and Envoy Gateway extensions are used to dynamically provision and configure the managed Envoy Proxies.

.. image:: /_static/envoy-gateway-overview.svg

.. note::

   For latest up to date Quickstart installation documentation visit the Envoy Gateway docs site on  `gateway.envoyproxy.io/docs/ <https://gateway.envoyproxy.io/docs/>`_.


Envoy Gateway Installation
--------------------------

This is a simple quickstart installation of Envoy Gteway with an example app.
At a minimum, you will need to have a Kubernetes cluster available to try this out.

Install the Gateway API CRDs and Envoy Gateway:

.. literalinclude:: _include/envoy-gateway-install.bash
    :language: bash
    :linenos:
    :lines: 1-3

Wait for Envoy Gateway to become available:

.. literalinclude:: _include/envoy-gateway-install.bash
    :language: bash
    :linenos:
    :lines: 3-5

Install the GatewayClass, Gateway, HTTPRoute and example app:

.. literalinclude:: _include/envoy-gateway-install.bash
    :language: bash
    :linenos:
    :lines: 5-7


.. note::
    ``quickstart.yaml`` defines that Envoy Gateway will listen for traffic on port 80 on its globally-routable IP address, to make it easy to use browsers to test Envoy Gateway. When Envoy Gateway sees that its Listener is using a privileged port (<1024), it will map this internally to an unprivileged port, so that Envoy Gateway doesn’t need additional privileges. It’s important to be aware of this mapping, since you may need to take it into consideration when debugging.


Testing the installation
------------------------

With External LoadBalancer Support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

You can also test the same functionality by sending traffic to the External IP. To get the external IP of the
Envoy service, run:

.. literalinclude:: _include/envoy-gateway-install.bash
    :language: bash
    :linenos:
    :lines: 9-11


In certain environments, the load balancer may be exposed using a hostname, instead of an IP address. If so, replace
`ip` in the above command with `hostname`.

Curl the example app through Envoy proxy:

.. literalinclude:: _include/envoy-gateway-install.bash
    :language: bash
    :linenos:
    :lines: 11-13


Without External LoadBalancer Support
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Get the name of the Envoy service created the by the example Gateway:

.. literalinclude:: _include/envoy-gateway-install.bash
    :language: bash
    :linenos:
    :lines: 15-17

Port forward to the Envoy service:

.. literalinclude:: _include/envoy-gateway-install.bash
    :language: bash
    :linenos:
    :lines: 17-19

Curl the example app through Envoy proxy:

.. literalinclude:: _include/envoy-gateway-install.bash
    :language: bash
    :linenos:
    :lines: 19-21


What to explore next?
---------------------

In this quickstart, you have:
- Installed Envoy Gateway
- Deployed a backend service, and a gateway
- Configured the gateway using Kubernetes Gateway API resources [Gateway](https://gateway-api.sigs.k8s.io/api-types/gateway/) and [HttpRoute](https://gateway-api.sigs.k8s.io/api-types/httproute/) to direct incoming requests over HTTP to the backend service.

Here is a suggested list of follow-on tasks to guide you in your exploration of Envoy Gateway:

- `HTTP Routing <https://gateway.envoyproxy.io/docs/tasks/traffic/http-routing>`_ 
- `Traffic Splitting <https://gateway.envoyproxy.io/docs/tasks/traffic/http-traffic-splitting>`_ 
- `Secure Gateways <https://gateway.envoyproxy.io/docs/tasks/security/secure-gateways/>`_ 
- `Global Rate Limit <https://gateway.envoyproxy.io/docs/tasks/traffic/global-rate-limit/>`_ 
- `gRPC Routing <https://gateway.envoyproxy.io/docs/tasks/traffic/grpc-routing/>`_ 

Find more introductions for configuring Envoy Proxy using the Envoy Gateway control plane under `Tasks <https://gateway.envoyproxy.io/docs/tasks/>`_ on the Envoy Gateway docs.


Clean-Up
--------

Use the steps in this section to uninstall everything from the quickstart.

Delete the GatewayClass, Gateway, HTTPRoute and Example App:

.. literalinclude:: _include/envoy-gateway-install.bash
    :language: bash
    :linenos:
    :lines: 23-25

Delete the Gateway API CRDs and Envoy Gateway:

.. literalinclude:: _include/envoy-gateway-install.bash
    :language: bash
    :linenos:
    :lines: 25-27


Get Help
---------

Join ``#gateway-users`` on Envoy Slack, get an invite to Envoy Slack `here <https://communityinviter.com/apps/envoyproxy/envoy>`_.