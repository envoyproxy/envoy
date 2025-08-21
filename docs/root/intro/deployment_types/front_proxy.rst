.. _deployment_type_front_proxy:

Service to service plus front proxy
-----------------------------------

.. image:: /_static/front_proxy.svg

The above diagram shows the :ref:`service to service <deployment_type_service_to_service>`
configuration sitting behind an Envoy cluster used as an HTTP L7 edge reverse proxy. The
reverse proxy provides the following features:

* Terminates TLS.
* Supports HTTP/1.1, HTTP/2, and HTTP/3.
* Full HTTP L7 routing support.
* Talks to the service to service Envoy clusters via the standard :ref:`ingress port
  <deployment_type_service_to_service_ingress>` and using the discovery service for host
  lookup. Thus, the front Envoy hosts work identically to any other Envoy host, other than the
  fact that they do not run collocated with another service. This means that are operated in the
  same way and emit the same statistics.

Configuration template
^^^^^^^^^^^^^^^^^^^^^^

The source distribution includes an example front proxy configuration. See
:ref:`here <install_sandboxes_front_proxy>` for more information.
