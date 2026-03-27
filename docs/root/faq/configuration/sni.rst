.. _faq_how_to_setup_sni:

How do I configure SNI for listeners?
=====================================

`SNI <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ is only supported in the :ref:`v3
configuration/API <config_overview>`.

.. attention::

  :ref:`TLS Inspector <config_listener_filters_tls_inspector>` listener filter must be configured
  in order to detect requested SNI.

The following is a YAML example of the above requirement.

.. literalinclude:: _include/sni-routing.yaml
    :language: yaml
    :lines: 2-67
    :lineno-start: 2
    :linenos:
    :caption: :download:`sni-routing.yaml <_include/sni-routing.yaml>`

How do I configure SNI for clusters?
====================================

See :ref:`SNI configuration <start_quick_start_securing_sni_client>` and :ref:`validation configuration <start_quick_start_securing_validation>`.
