The shared pool of endpoint and locality metadata now hashes and compares metadata by its
deterministic serialization instead of by reflection, and a host computes its metadata hash only
when an update compares it, which makes EDS updates with endpoint metadata cheaper. Metadata whose
``google.protobuf.Any`` payloads were serialized differently now gets its own pooled copy, while an
update still treats it as unchanged.
