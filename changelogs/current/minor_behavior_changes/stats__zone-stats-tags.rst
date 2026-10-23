Added the new ``envoy.upstream_from_zone`` and ``envoy.upstream_to_zone`` tags, extracted by the
default stats tag-extraction rules from a cluster's upstream per-zone stats
(``cluster.<name>.zone.<from_zone>.<to_zone>.*``). Flat stat names are unchanged.
