Fixed a bug introduced in 1.38.0 that exported Prometheus native histogram bucket indices one bucket
too low, causing consumers to decode observations into lower value ranges and underreport quantiles.
