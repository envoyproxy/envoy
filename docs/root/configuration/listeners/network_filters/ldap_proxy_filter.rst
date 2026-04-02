.. _config_network_filters_ldap_proxy:

LDAP proxy
==========

The LDAP proxy filter decodes the wire protocol between an LDAP client
(downstream) and an LDAP server (upstream) to detect StartTLS Extended Requests
(OID ``1.3.6.1.4.1.1466.20037``, RFC 4511). When a StartTLS negotiation
succeeds, the filter switches the connection's transport sockets from cleartext
to TLS and then enters passthrough mode.

.. attention::

   The ``ldap_proxy`` filter is experimental and is currently under active development.
   Capabilities will be expanded over time and the configuration structures are likely to change.

Operating modes
---------------

The filter supports two ``upstream_starttls_mode`` values:

.. csv-table::
  :header: Mode, Description
  :widths: 1, 3

  ``ON_DEMAND``, "The filter waits for the downstream client to send a StartTLS
  Extended Request. When detected, it forwards the request to the upstream server,
  waits for a success response, upgrades **both** the upstream and downstream
  connections to TLS, and then enters passthrough mode."
  ``ALWAYS``, "On the very first downstream data, the filter proactively sends a
  StartTLS request to the upstream server. Once the upstream agrees, only the
  **upstream** side is upgraded to TLS; the downstream side stays plaintext. This
  is useful when clients cannot or should not perform StartTLS themselves."

After the TLS transition (or if a plaintext operation is detected in ON_DEMAND
mode), the filter moves to **passthrough** and no longer inspects traffic.

Configuration
-------------

The LDAP proxy filter should be chained with the TCP proxy filter. An upstream
``starttls`` transport socket is required so that the connection can transition
from cleartext to TLS after the StartTLS handshake.

ALWAYS mode
~~~~~~~~~~~

In ALWAYS mode the filter proactively sends StartTLS to the upstream on the
first downstream data. No downstream transport socket is needed because the
client-facing connection stays plaintext:

.. literalinclude:: _include/ldap-proxy-always.yaml
   :language: yaml
   :linenos:
   :caption: :download:`ldap-proxy-always.yaml <_include/ldap-proxy-always.yaml>`

ON_DEMAND mode
~~~~~~~~~~~~~~

In ON_DEMAND mode a downstream ``starttls`` transport socket is also needed so
that Envoy can upgrade the client-facing connection to TLS after the negotiation:

.. literalinclude:: _include/ldap-proxy-on-demand.yaml
   :language: yaml
   :linenos:
   :caption: :download:`ldap-proxy-on-demand.yaml <_include/ldap-proxy-on-demand.yaml>`

.. _config_network_filters_ldap_proxy_stats:

Statistics
----------

Every configured LDAP proxy filter has statistics rooted at
*ldap.<stat_prefix>.* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 2, 1, 3

  starttls_req_total, Counter, Total number of StartTLS requests initiated (by client in ON_DEMAND or by filter in ALWAYS)
  starttls_rsp_total, Counter, Total number of upstream StartTLS responses received
  starttls_rsp_success, Counter, Number of successful upstream StartTLS responses (resultCode=0)
  starttls_rsp_error, Counter, "Number of failed upstream StartTLS responses (parse error, msgID mismatch, non-zero resultCode, or timeout)"
  decoder_error, Counter, Number of BER decoding errors or internal protocol errors that caused connection closure
  protocol_violation, Counter, Number of pipeline violations (data received during StartTLS negotiation)
  starttls_op_time, Histogram, "Time in milliseconds for the full StartTLS operation, from request sent to TLS transition complete"
