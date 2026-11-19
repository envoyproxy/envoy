.. _config_tls_certificate_selectors_dynamic_modules:

Dynamic modules TLS certificate selector
========================================

The :ref:`DynamicModuleTlsCertificateSelector
<envoy_v3_api_msg_extensions.transport_sockets.tls.cert_selectors.dynamic_modules.v3.DynamicModuleTlsCertificateSelector>`
configuration specifies a :ref:`custom TLS certificate selector
<envoy_v3_api_field_extensions.transport_sockets.tls.v3.CommonTlsContext.custom_tls_certificate_selector>`
backed by a :ref:`dynamic module <arch_overview_dynamic_modules>`. For each handshake the module
selects one of the configured certificates, lets Envoy select one, or selects a dynamic certificate
that it provides at runtime.

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.transport_sockets.tls.cert_selectors.dynamic_modules.v3.DynamicModuleTlsCertificateSelector>`

Once the TLS context is ready to serve handshakes, its dynamic certificates are listed by the
:ref:`/certs <operations_admin_interface_certs>` admin endpoint, without the trusted certificates that
the TLS context lists itself, and they count in the ``server.days_until_first_cert_expiring`` and
``server.seconds_until_first_ocsp_response_expiring`` statistics.

Statistics
----------

With :ref:`allow_dynamic_certificates
<envoy_v3_api_field_extensions.transport_sockets.tls.cert_selectors.dynamic_modules.v3.DynamicModuleTlsCertificateSelector.allow_dynamic_certificates>`
set, the selector emits the following statistics. For downstream listeners, they are in the
*listener.<address>.dynamic_certificates.* namespace, or *listener.<stat_prefix>.dynamic_certificates.*
if :ref:`stat_prefix <envoy_v3_api_field_config.listener.v3.Listener.stat_prefix>` is non-empty. For
filter chains from the Filter Chain Discovery Service (FCDS), they are in the *dynamic_certificates.*
namespace. For upstream clusters, they are in the *cluster.<name>.dynamic_certificates.* namespace,
or *cluster.<alt_stat_name>.dynamic_certificates.* if :ref:`alt_stat_name
<envoy_v3_api_field_config.cluster.v3.Cluster.alt_stat_name>` is non-empty.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  requested, Counter, Total number of dynamic certificate requests to the module
  provided, Counter, Total number of dynamic certificates that the module provided and Envoy accepted
  rejected, Counter, Total number of dynamic certificates that the module provided and Envoy rejected
  removed, Counter, Total number of dynamic certificates removed by the module or because they failed with an updated TLS context
  request_timeout, Counter, Total number of dynamic certificate requests that the module did not answer in time
  active, Gauge, Number of dynamic certificates that are served
  pending, Gauge, Number of dynamic certificate requests that wait for the module
