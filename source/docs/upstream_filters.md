# Converting a downstream filter to be a dual filter

Before coverting a filter to be an upstream filter you should do some basic
functionality analysis. Make sure

  * The filter does not use any downstream-only functionality, such as clearing
    cached routes, or performing internal redirects.
  * Either the filter does not sendLocalReply, or you thoroughly document how
    sendLocalReply code paths will play with hedging / retries (cut off the
    hedge attempt, and local-reply failures won't trigger retries)

Once you've done this, you're ready to convert. An example converted filter is the Envoy
[Buffer](https://github.com/envoyproxy/envoy/blob/main/source/extensions/filters/http/buffer/config.cc)
[Filter](https://github.com/envoyproxy/envoy/blob/main/source/extensions/filters/http/buffer/config.h)

Assuming your filter inherits from FactoryBase:

  * Change from inheriting from FactoryBase to inheriting from DualFactoryBase
  * This changes the type of createFilterFactoryFromProto.  Update accordingly.
    Please note if you use the init manager or a stats context you *must* get
    them from DualInfo rather than from the server factory context or xDS
    reloads will not work correctly and may leak memory.
  * Add ``UpstreamMyFilterFactory = MyFilterFactory;`` and
    ``DECLARE_FACTORY(UpstreamMyFilterFactory`` in your config.h file and
    ``REGISTER_FACTORY(UpstreamMyFilterFactory,
                     Server::Configuration::UpstreamHttpFilterConfigFactory){"envoy.my_filter"};`` to
    your config.cc file.
  * If your filter is listed in ``source/extensions/extensions_metadata.yaml``
    add ``envoy.filters.http.upstream`` to the filter category.

Your filter should now be available as an upstream filter.
