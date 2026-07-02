Added the :ref:`upstream RBAC HTTP filter <config_http_filters_upstream_rbac>`, an upstream HTTP
filter that evaluates RBAC policies against the selected upstream host before the upstream
connection is initiated. This enables the :ref:`upstream_ip_port
<envoy_v3_api_msg_extensions.rbac.matchers.upstream_ip_port.v3.UpstreamIpPortMatcher>` matcher for
all cluster types (including dynamic forward proxy ``sub_cluster_config``, static, EDS, and strict
DNS), allowing default-deny SSRF protection that rejects requests to disallowed upstream addresses.
