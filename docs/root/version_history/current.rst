1.15.0 (Pending)
================

Changes
-------

* http: fixed a bug where the upgrade header was not cleared on responses to non-upgrade requests.
  Can be reverted temporarily by setting runtime feature `envoy.reloadable_features.fix_upgrade_response` to false.
* network filters: added a :ref:`postgres proxy filter <config_network_filters_postgres_proxy>`.

Deprecated
----------

