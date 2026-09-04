.. _deployment_type_double_proxy:

Service to service, front proxy, and double proxy
-------------------------------------------------

.. image:: /_static/double_proxy.svg
  :width: 70%

The above diagram shows the :ref:`front proxy <deployment_type_front_proxy>` configuration alongside
another Envoy cluster running as a *double proxy*. The idea behind the double proxy is that it is
more efficient to terminate TLS and client connections as close as possible to the user (shorter
round trip times for the TLS handshake, faster TCP CWND expansion, less chance for packet loss,
etc.). Connections that terminate in the double proxy are then multiplexed onto long lived HTTP/2
or HTTP/3 connections running in the main data center.

In the above diagram, the front Envoy proxy running in region 1 authenticates itself with the front
Envoy proxy running in region 2 via TLS mutual authentication and pinned certificates. This allows
the front Envoy instances running in region 2 to trust elements of the incoming requests that
ordinarily would not be trustable (such as the x-forwarded-for HTTP header).

Configuration template
^^^^^^^^^^^^^^^^^^^^^^

The source distribution includes an example double proxy configuration. See
:ref:`here <install_sandboxes_double_proxy>` for more information.
