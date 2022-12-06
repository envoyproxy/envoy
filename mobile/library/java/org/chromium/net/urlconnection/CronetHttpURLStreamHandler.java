package org.chromium.net.urlconnection;

import java.io.IOException;
import java.net.Proxy;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLStreamHandler;
import org.chromium.net.ExperimentalCronetEngine;

/**
 * A {@link URLStreamHandler} that handles HTTP and HTTPS connections. One can use this class to
 * create {@link java.net.HttpURLConnection} instances implemented by Cronet; for example: <pre>
 *
 * CronetHttpURLStreamHandler streamHandler = new CronetHttpURLStreamHandler(myContext);
 * HttpURLConnection connection = (HttpURLConnection)streamHandler.openConnection(
 *         new URL("http://chromium.org"));</pre>
 * <b>Note:</b> Cronet's {@code HttpURLConnection} implementation is subject to some limitations
 * listed {@link CronetURLStreamHandlerFactory here}.
 */
final class CronetHttpURLStreamHandler extends URLStreamHandler {
  private final ExperimentalCronetEngine mCronetEngine;

  public CronetHttpURLStreamHandler(ExperimentalCronetEngine cronetEngine) {
    mCronetEngine = cronetEngine;
  }

  /**
   * Establishes a new connection to the resource specified by the {@link URL} {@code url}.
   * @return an {@link java.net.HttpURLConnection} instance implemented by Cronet.
   */
  @Override
  public URLConnection openConnection(URL url) throws IOException {
    return mCronetEngine.openConnection(url);
  }

  /**
   * Establishes a new connection to the resource specified by the {@link URL} {@code url}
   * using the given proxy.
   * @return an {@link java.net.HttpURLConnection} instance implemented by Cronet.
   */
  @Override
  public URLConnection openConnection(URL url, Proxy proxy) throws IOException {
    return mCronetEngine.openConnection(url, proxy);
  }
}
