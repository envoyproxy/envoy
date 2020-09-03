.. _testing_local_stats:

Local Stats
===========

The `local-stats` `branch <https://github.com/lyft/envoy-mobile/tree/local-stats>`_ allows a
developer to run a local statsd server, and see stats emissions from a client running in the
simulator/emulator.

------
Config
------

The `config template <https://github.com/lyft/envoy-mobile/blob/local-stats/library/common/config_template.cc>`_
has already been updated to use a local statsd server. However, if you are using Android to test,
the `static address <https://github.com/lyft/envoy-mobile/blob/local-stats/library/common/config_template.cc#L203>`_
used for the server should be changed to ``10.0.2.2`` per the `Set up Android Emulator networking <https://developer.android.com/studio/run/emulator-networking>`_
docs.

-----
Steps
-----

1. Build the desired dist target following the build docs :ref:`here <building>`.
2. Start statsd locally. You can do this by cloning `statsd <https://github.com/statsd/statsd>`_ and running from within the root of
   the repo with::

    node stats.js config.js

  An example ``config.js`` file. Note that the port must match the port in the `config_template <https://github.com/lyft/envoy-mobile/blob/local-stats/library/common/config_template.cc#L203>`_::

    {
      port: 8125
    , backends: [ "./backends/console" ]
    , servers: [{server: "./servers/tcp", debug: true}]
    , debug: true
    }

3. Run the :ref:`example <hello_world>` app.
4. Stats should appear in the terminal window you are running ``statsd`` in.
