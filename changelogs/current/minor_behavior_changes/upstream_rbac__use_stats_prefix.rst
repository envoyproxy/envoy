The :ref:`upstream RBAC filter <envoy_v3_api_msg_extensions.filters.http.rbac.v3.RBAC>` now
correctly scopes its stats under the parent's stat prefix. When used as a router upstream filter
the stats appear under ``http.<stat_prefix>.rbac.*`` (e.g.
``http.ingress_http.rbac.allowed``); when used as a cluster upstream filter they continue to
appear under ``cluster.<name>.rbac.*``. Previously the filter always emitted stats with no
context prefix.
