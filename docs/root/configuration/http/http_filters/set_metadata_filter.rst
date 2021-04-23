.. _config_http_filters_set_metadata:

Set Metadata
============
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.set_metadata.v3.Config>`
* This filter should be configured with the name *envoy.filters.http.set_metadata*.

This filters adds or updates dynamic metadata with static data.

Dynamic metadata values are updated with the following scheme. If a key
does not exists, it's just copied into the current metadata. Otherwise:

 * scalar values (null, string, number, boolean) are replaced with the new value
 * list: new values are added to the current list
 * structure: recursively apply this scheme

For instance, if the namespace already contains this structure:

.. code-block:: yaml

   myint: 1
   mylist: ["a"]
   mytags:
     tag0: 1

and the value to set is:

.. code-block:: yaml

   myint: 2
   mylist: ["b","c"]
   mytags:
     tag1: 1

After applying this filter, the namespace will contain:

.. code-block:: yaml

   myint: 2
   mylist: ["a","b","c"]
   mytags:
     tag0: 1
     tag1: 1
 
Statistics
----------

Currently, this filter generates no statistics.
