Fixed a bug in the geoip filters where lookups would be attempted on non-IP addresses,
such as internal or Unix domain socket addresses. The filters now skip the lookup when no IP address
is available and increment the ``skipped`` counter.
