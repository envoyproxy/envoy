The HTTP filter factory base classes ``FactoryBase``, ``ExceptionFreeFactoryBase``, and
``DualFactoryBase``, together with the ``createFilterFactoryFromProto()`` entry points on
``NamedHttpFilterConfigFactory`` and ``UpstreamHttpFilterConfigFactory``, are deprecated in favor
of ``UnifiedFactoryBase`` and its single ``createHttpFilterFactoryFromProtoTyped()`` entry point,
which serves both the downstream and the upstream HTTP filter chains. This only affects extension
code, not configuration. ``createFilterFactoryFromProto()`` is no longer pure virtual: it now
defaults to delegating to ``createHttpFilterFactoryFromProto()``, so a factory that implements the
interfaces directly only needs to implement the new entry point. The deprecated classes and methods
keep working and will be removed once the in-tree and out-of-tree extensions have migrated. Note
that Envoy itself builds with ``-Wno-deprecated-declarations``, so these deprecations are only
visible to out-of-tree builds that enable the warning; such builds can pass
``-Wno-deprecated-declarations`` to keep compiling while the migration is in progress.
