# Converting a downstream HTTP filter to be a dual filter

Before coverting a filter to be an upstream HTTP filter you should do some basic
functionality analysis. Make sure

  * The filter does not use any downstream-only functionality, accessed via
    downstreamCallbacks() such as clearing cached routes, or performing internal redirects.
  * Either the filter does not sendLocalReply, or you test and document how
    sendLocalReply code paths will play with hedging / retries (cut off the
    hedge attempt, and local-reply failures won't trigger retries)
  * Either the filter does not access streamInfo in a non-const way, or you test
    and document how the filter interacts with hedging and retries. Note that
    for hedging, a single downstream StreamInfo is accessible in parallel to
    both instances of the upstream HTTP filter instance, so it must be resiliant to
    parallel access.
  * Any code accessing the downstream connection checks to make sure it is
    present. The downstream connection will not be available for mirrored/shadowed requests.

Once you've done this, you're ready to convert. An example converted filter is the Envoy
[Buffer](https://github.com/envoyproxy/envoy/blob/main/source/extensions/filters/http/buffer/config.cc)
[Filter](https://github.com/envoyproxy/envoy/blob/main/source/extensions/filters/http/buffer/config.h)

Assuming your filter inherits from FactoryBase:

  * Change from inheriting from FactoryBase to inheriting from DualFactoryBase
  * This changes the type of createFilterFactoryFromProto.  Update accordingly.
    Please note if you use the init manager or a stats context you *must* get
    them from DualInfo rather than from the server factory context or xDS
    reloads will not work correctly and may leak memory.
  * Add ``using UpstreamMyFilterFactory = MyFilterFactory;`` in your config.h file and
    ``REGISTER_FACTORY(UpstreamMyFilterFactory, Server::Configuration::UpstreamHttpFilterConfigFactory);`` to
    your config.cc file.
  * If your filter is listed in ``source/extensions/extensions_metadata.yaml``
    add ``envoy.filters.http.upstream`` to the filter category.

Your filter should now be available as an upstream HTTP filter.

An example PR for conversion is [23071](https://github.com/envoyproxy/envoy/pull/23071)
