Reduced temporary memory used to serialize Prometheus text responses by rendering metrics
incrementally while preserving metric-family grouping. Text responses capture metric values before
generating chunks so consumer delays do not affect sampling. Protobuf responses are unchanged.
