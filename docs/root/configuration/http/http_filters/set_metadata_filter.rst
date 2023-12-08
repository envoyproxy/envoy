.. _config_http_filters_set_metadata:

Set Metadata
============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.set_metadata.v3.Config>`

This filters adds or updates dynamic metadata with static data.

Typed dynamic metadata will overwrite existing values at the configured namespace if and only if ``allow_overwrite`` is
set to true, otherwise nothing is done. If the namespace does not already exist in the typed dynamic
metadata, it is inserted with the configured value.

If ``allow_overwrite`` is set to false or omitted, untyped dynamic metadata will only be updated if
there are no existing values at that namespace. If ``allow_overwrite`` is set to true, or if using deprecated ``value``
field, untyped dynamic metadata values are updated with the following scheme:
- If a key does not exist, the configured value is copied into the metadata namespace.
- If the key exists but has a different type, the value is replaced outright by the configured value.
- Otherwise:
 * scalar values (null, string, number, boolean): the existing value is replaced.
 * lists: new values are appended to the current list.
 * structures: recursively apply this scheme.

For instance, if the namespace already contains this structure:

.. code-block:: yaml

   myint: 1
   mylist: ["a"]
   mykey: ["val"]
   mytags:
     tag0: 1

and the untyped_metadata.value to set is:

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

Currently, this filter generates no statistics.
