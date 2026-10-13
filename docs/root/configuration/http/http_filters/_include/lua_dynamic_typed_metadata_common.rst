**Type Conversion Details**

The following rules apply when converting protocol buffer messages into Lua tables:

* Repeated fields are converted to Lua arrays (1-based indexing).
* Map fields become Lua tables with string keys.
* Enums are represented as their numeric values.
* Byte fields are translated to Lua strings.
* Nested messages are converted to nested tables.
* Optional fields that are not set are returned as ``nil``.

**Error Handling**

This method ensures type-safe access to metadata but returns ``nil`` in the following scenarios:

* If the specified filter name does not exist. For example, trying to access a filter name when that filter isn't configured.
* If the metadata exists but cannot be unpacked. It could happen if the filter state exists but is stored as a different type than expected.
* If the protocol buffer message is malformed. It could happen when the data in the filter state is corrupted or partially written.

**Limitations**

1. Dynamic typed metadata is read-only and cannot be modified through this API.
2. Raw protobuf message structure cannot be accessed directly.
3. Extension types or unknown fields cannot be accessed through this API.
4. Map keys must be strings or integers.
5. Some protocol buffer features (like Any messages) may not be fully supported.
