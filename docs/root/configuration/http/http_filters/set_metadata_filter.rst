.. _config_http_filters_set_metadata:

Set Metadata
============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.set_metadata.v3.Config>`

This filters adds or updates dynamic metadata with static data.

Dynamic metadata values are updated with the following rules. If a key does not exist, it is copied into the current metadata. If the key exists, then following rules will be used:

* if :ref:`typed metadata value <envoy_v3_api_field_extensions.filters.http.set_metadata.v3.Metadata.typed_value>` is used, it will overwrite existing values iff :ref:`allow_overwrite <envoy_v3_api_field_extensions.filters.http.set_metadata.v3.Metadata.allow_overwrite>` is set to true, otherwise nothing is done.
* if :ref:`untyped metadata value <envoy_v3_api_field_extensions.filters.http.set_metadata.v3.Metadata.value>` is used and ``allow_overwrite`` is set to true, or if deprecated :ref:`value <envoy_v3_api_field_extensions.filters.http.set_metadata.v3.Config.value>` field is used, the values are updated with the following scheme:
  - existing value with different type: the existing value is replaced.
  - scalar values (null, string, number, boolean): the existing value is replaced.
  - lists: new values are appended to the current list.
  - structures: recursively apply this scheme.

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

Statistics
----------

The ``set_metadata`` filter outputs statistics in the ``http.<stat_prefix>.set_metadata.`` namespace. The :ref:`stat prefix
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.stat_prefix>` comes from the
owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  overwrite_denied, Counter, Total number of denied attempts to overwrite an existing metadata value
