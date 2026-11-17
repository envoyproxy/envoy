Fixed the ``mcp`` filter's streaming JSON field extractor losing track of the current field path after
an object with an empty key (``{"": {}}``). The empty-key object popped its parent's path segment, so
every field that followed it, such as ``params.name``, was resolved against the wrong path and not
extracted. Push and pop of the path stack are now paired for every object regardless of its key.
