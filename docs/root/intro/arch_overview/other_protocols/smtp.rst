.. _arch_overview_smtp:

SMTP
====

Envoy supports a network level SMTP_ filter to support TLS termination
both upstream and downstream in front of a client or server that
doesn't implement the SMTP STARTTLS extension_.

.. _SMTP: http://rfc-editor.org/rfc/rfc5321
.. _extension: http://rfc-editor.org/rfc/rfc3207

The primary expected use case is to sidecar with a lightweight SMTP
application that doesn't want to deal with TLS internally. This may
also be useful in more complex deployments of Mail Transfer Agents
(e.g. Postfix or Exim) to move TLS termination to the edge.

SMTP proxy filter :ref:`configuration reference <config_network_filters_smtp_proxy>`.


