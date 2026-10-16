Added stats under ``http.<stat_prefix>.on_demand.`` to the :ref:`on demand filter
<config_http_filters_on_demand>` counting route and cluster discoveries by outcome, in-flight and
cancelled discoveries and their duration, and the
``envoy.filters.http.on_demand.{route,cluster}_discovery_{start,end}`` downstream timing keys for use with
``%COMMON_DURATION%``.
