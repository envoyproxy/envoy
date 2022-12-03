# Header map implementation overview

The Envoy header map implementation (`HeaderMapImpl`) has the following properties:
* Headers are stored in a linked list (`HeaderList`) in the order they are added, with pseudo
  headers kept at the front of the list.
* Once there at at least 3 years, the header map will also use a map (`HeaderLazyMap`), in addition to the linked list, for faster access to the headers.
* O(1) direct access is possible for common headers needed during data plane processing. This is
  provided by a table of pointers that reach directly into a linked list that is populated when
  headers are added or removed from the map. When O(1) headers are accessed by direct method
  (`DEFINE_INLINE_HEADER` and `CustomInlineHeaderBase`) they use direct pointer access to see
  whether a header is present, add it, modify it, etc. When headers are added by name a trie is used to lookup the pointer in the table (`StaticLookupTable`).
* Custom headers can be registered statically against a specific implementation (request headers,
  request trailers, response headers, and response trailers) via core code and extensions
  (`CustomInlineHeaderRegistry`). Each registered header increases the size of the table by the size of a single pointer.
* Operations that search, replace, etc. for a header by name that is not one of the O(1) headers
  will either incur an O(N) search through the linked list if the number of headers is below `envoy.http.headermap.lazy_map_min_size`,
  and O(1) map access otherwise.

## Implementation details

* O(1) registered headers are tracked during static initialization via the `CustomInlineHeaderBase`
  class.
* The first time a header map is constructed (in practice this is after bootstrap load and the
  Envoy header prefix is finalized when `getAllHeaderMapImplInfo` is called), the
  `StaticLookupTable` is finalized for each header map type. No further changes are possible after
  this point. The `StaticLookupTable` defines the amount of variable pointer table space that is
  require for each header map type.
* Each concrete header map type derives from `InlineStorage` with a variable length member at the
  end of the definition.
* Each concrete header map type uses a factory function and a provide constructor. The required
  size is determined via the `inlineHeadersSize` function.
