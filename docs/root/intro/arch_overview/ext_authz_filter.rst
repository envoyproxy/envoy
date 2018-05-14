.. _arch_overview_ext_authz:

External Authorization
======================

* :ref:`Network filter v2 API reference <envoy_api_msg_config.filter.network.ext_authz.v2.ExtAuthz>`
* :ref:`HTTP filter v2 API reference <envoy_api_msg_config.filter.http.ext_authz.v2alpha.ExtAuthz>`

The External authorization filter calls an authorization service to check if the incoming request is authorized or not. The filter can be either configured either as a :ref:`network filter <config_network_filters_ext_authz>`, as a :ref:`HTTP filter <config_http_filters_ext_authz>` or both. If the request is deemed unauthorized by the network filter then the connection will be closed. If the request is deemed unauthorized at the HTTP filter the request will be denied with 403 (Forbidden) response.

It is recommended that these filter are configured as the first filters in the filter chain so that requests are authorized prior to rest of the filters running through the request.

The external authorization service cluster may be either statically configured or via :ref:`Cluster Discovery Service <config_cluster_manager_cds>`. If the external service is not available when a request comes in then whether the request is authorized or not is defined by the configuration setting of *failure_mode_allow* configuration in the applicable :ref:`network filter <envoy_api_msg_config.filter.network.ext_authz.v2.ExtAuthz>` or :ref:`HTTP filter <envoy_api_msg_config.filter.http.ext_authz.v2alpha.ExtAuthz>`. If it is set to true then the request will be permitted (fail open) otherwise it will be denied. The default setting is *false*.

Service Definition
==================

The context of the traffic is passed on to an external authorization service using the service definition listed here.
The content of the request that are passed to an authorization service is specified by :ref:`CheckRequest <envoy_api_msg_service.auth.v2alpha.CheckRequest>`

.. toctree::
  :glob:
  :maxdepth: 2

  ../../api-v2/service/auth/v2alpha/*
