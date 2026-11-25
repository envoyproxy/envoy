EDS updates now reuse the existing host for an endpoint whose address and all other host fields are unchanged,
instead of building a new host that is then discarded. This lowers the main thread cost of an update in which most
endpoints are unchanged and carry no endpoint metadata, by about 17% at 100k endpoints. An update that changes the
health or the metadata of every endpoint costs about 10% more, and endpoints with a health check config or
additional addresses always get a new host. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.eds_reuse_unchanged_hosts`` to false.
