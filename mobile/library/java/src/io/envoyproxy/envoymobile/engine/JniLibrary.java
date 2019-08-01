package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyData;
import io.envoyproxy.envoymobile.engine.types.EnvoyHeaders;
import io.envoyproxy.envoymobile.engine.types.EnvoyObserver;
import io.envoyproxy.envoymobile.engine.types.EnvoyStream;

class JniLibrary {

  protected static native EnvoyStream startStream(EnvoyObserver observer);

  protected static native int sendHeaders(EnvoyStream stream, EnvoyHeaders headers,
                                          boolean endStream);

  protected static native int sendData(EnvoyStream stream, EnvoyData data, boolean endStream);

  protected static native int sendMetadata(EnvoyStream stream, EnvoyHeaders metadata,
                                           boolean endStream);

  protected static native int sendTrailers(EnvoyStream stream, EnvoyHeaders trailers);

  protected static native int locallyCloseStream(EnvoyStream stream);

  protected static native int resetStream(EnvoyStream stream);

  protected static native int runEngine(String config, String logLevel);
}
