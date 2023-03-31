.. _arch_overview_smtp:

SMTP
========

Envoy supports a network level SMTP sniffing filter developed as per `RFC 5321 <https://www.rfc-editor.org/rfc/rfc5321>`_. It decodes SMTP commands and responses and provides useful statistics.
It also supports STARTTLS extension `RFC 3207 <https://www.ietf.org/rfc/rfc3207.txt>`_. The filter can terminated tls for downstream connection. If enabled, it also also supports switching upstream connection to TLS with the help of starttls transport socket.

The filter supports following features:

* STARTTLS command handling, configuration to enable upstream starttls.
* SMTP protocol level statistics


SMTP proxy filter :ref:`configuration reference <config_network_filters_smtp_proxy>`.
