1.18.0 (April 15, 2021)
=======================

Bug Fixes
---------

* code: fixed some whitespace to make fix_format happy.

New Features
------------

* http: added support for :ref:`original IP detection extensions<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`.
  Two initial extensions were added, the :ref:`custom header <envoy_v3_api_msg_extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig>` extension and the
  :ref:`xff <envoy_v3_api_msg_extensions.http.original_ip_detection.xff.v3.XffConfig>` extension.

Deprecated
----------

* http: :ref:`xff_num_trusted_hops <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.xff_num_trusted_hops>` is deprecated in favor of :ref:`original IP detection extensions<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.original_ip_detection_extensions>`.
