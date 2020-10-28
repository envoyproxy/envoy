.. _start_quick_start_dynamic_filesystem:

Dynamic configuration (filesystem)
==================================

``node``
--------

The :ref:`node <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.node>` should specify ``cluster`` and ``id``.

.. literalinclude:: _include/envoy-dynamic-filesystem-demo.yaml
    :language: yaml
    :linenos:
    :lines: 1-5
    :emphasize-lines: 1-3

``dynamic_resources``
---------------------

.. literalinclude:: _include/envoy-dynamic-filesystem-demo.yaml
    :language: yaml
    :linenos:
    :lines: 3-11
    :lineno-start: 3
    :emphasize-lines: 3-7

``admin``
---------

.. literalinclude:: _include/envoy-dynamic-filesystem-demo.yaml
    :language: yaml
    :linenos:
    :lines: 9-16
    :lineno-start: 9
    :emphasize-lines: 3-8

``resources`` - Listeners Discovery Service (LDS)
-------------------------------------------------

.. literalinclude:: _include/envoy-dynamic-lds-demo.yaml
    :language: yaml
    :linenos:

``resources`` - Clusters Discovery Service (CDS)
------------------------------------------------

.. literalinclude:: _include/envoy-dynamic-cds-demo.yaml
    :language: yaml
    :linenos:
