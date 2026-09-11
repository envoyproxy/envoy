.. _config_http_filters_set_metadata:

Set Metadata
============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.set_metadata.v3.Config>`

The Set Metadata filter adds or updates dynamic metadata with static or dynamically formatted data.
This filter is useful for attaching contextual information to requests that can be consumed by other
filters, used for load balancing decisions, included in access logs, or utilized for routing decisions.

The filter supports both untyped metadata (using ``google.protobuf.Struct``) and typed metadata
(using ``google.protobuf.Any``). Dynamic metadata values are updated according to specific merge
rules that vary based on the value type and the ``allow_overwrite`` configuration.

Common use cases include:

* Tagging requests with environment or service information for downstream processing.
* Adding routing hints for load balancer subset selection.
* Enriching access logs with additional context.
* Storing computed values for use by subsequent filters in the chain.

Configuration
-------------

The filter can be configured with multiple metadata entries, each targeting a specific namespace.
Each entry can contain either static values or use the deprecated legacy configuration format.

Metadata Merge Rules
--------------------

Dynamic metadata values are updated with the following rules. If a key does not exist, it is copied into the current metadata. If the key exists, the following rules apply:

* **Typed metadata values**: When :ref:`typed_value <envoy_v3_api_field_extensions.filters.http.set_metadata.v3.Metadata.typed_value>` is used, it will overwrite existing values if and only if :ref:`allow_overwrite <envoy_v3_api_field_extensions.filters.http.set_metadata.v3.Metadata.allow_overwrite>` is set to ``true``. Otherwise, the operation is skipped.

* **Untyped metadata values**: When :ref:`value <envoy_v3_api_field_extensions.filters.http.set_metadata.v3.Metadata.value>` is used and ``allow_overwrite`` is set to ``true``, or when the deprecated :ref:`value <envoy_v3_api_field_extensions.filters.http.set_metadata.v3.Config.value>` field is used, values are merged using the following scheme:

  - **Different type values**: The existing value is replaced entirely.
  - **Scalar values** (null, string, number, boolean): The existing value is replaced.
  - **Lists**: New values are appended to the existing list.
  - **Structures**: The merge rules are applied recursively to nested structures.

Merge Example
^^^^^^^^^^^^^

For instance, if the namespace already contains this structure:

.. code-block:: yaml

   myint: 1
   mylist: ["a"]
   mykey: ["val"]
   mytags:
     tag0: 1

and the value to set is:

.. code-block:: yaml

   myint: 2
   mylist: ["b","c"]
   mykey: 1
   mytags:
     tag1: 1

After applying this filter, the namespace will contain:

.. code-block:: yaml

   myint: 2
   mylist: ["a","b","c"]
   mykey: 1
   mytags:
     tag0: 1
     tag1: 1

Configuration Examples
----------------------

Basic Static Metadata
^^^^^^^^^^^^^^^^^^^^^^

A simple configuration that adds static metadata to the ``envoy.lb`` namespace:

.. literalinclude:: _include/set-metadata-basic-static.yaml
    :language: yaml
    :lines: 29-39
    :lineno-start: 29
    :linenos:
    :caption: :download:`set-metadata-basic-static.yaml <_include/set-metadata-basic-static.yaml>`

Multiple Metadata Entries
^^^^^^^^^^^^^^^^^^^^^^^^^^

Configuration with multiple metadata entries targeting different namespaces:

.. literalinclude:: _include/set-metadata-multiple-entries.yaml
    :language: yaml
    :lines: 29-44
    :lineno-start: 29
    :linenos:
    :caption: :download:`set-metadata-multiple-entries.yaml <_include/set-metadata-multiple-entries.yaml>`

Typed Metadata Configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Configuration using typed metadata with ``google.protobuf.Any``:

.. literalinclude:: _include/set-metadata-typed-configuration.yaml
    :language: yaml
    :lines: 29-39
    :lineno-start: 29
    :linenos:
    :caption: :download:`set-metadata-typed-configuration.yaml <_include/set-metadata-typed-configuration.yaml>`

Overwrite Control
^^^^^^^^^^^^^^^^^

Configuration demonstrating overwrite control behavior:

.. literalinclude:: _include/set-metadata-overwrite-control.yaml
    :language: yaml
    :lines: 29-49
    :lineno-start: 29
    :linenos:
    :caption: :download:`set-metadata-overwrite-control.yaml <_include/set-metadata-overwrite-control.yaml>`

.. note::

   In the above example, the final metadata will contain:
   ``counter: 3``, ``list: ["first", "third"]``, and ``new_field: "added"``.
   The second entry is ignored because ``allow_overwrite`` is not set.

Statistics
----------

The Set Metadata filter outputs statistics in the ``http.<stat_prefix>.set_metadata.`` namespace.
The :ref:`stat prefix <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>`
comes from the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  overwrite_denied, Counter, Total number of denied attempts to overwrite an existing metadata value when ``allow_overwrite`` is ``false``
