.. _config_http_filters_set_metadata:

Set Metadata
============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.set_metadata.v3.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.set_metadata.v3.Config>`

This filters adds or updates dynamic metadata with static data.

Dynamic metadata values are updated with the following scheme. If a key
does not exists, it's just copied into the current metadata. If the key exists
but has a different type, it is replaced by the new value. Otherwise:

 * for scalar values (null, string, number, boolean) are replaced with the new value
 * for lists: new values are added to the current list
 * for structures: recursively apply this scheme

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

Currently, this filter generates no statistics.
