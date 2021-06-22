package org.chromium.net.testing;

import static junit.framework.Assert.assertTrue;

import org.chromium.net.CronetEngine;

/**
 * Helper class to set up url interceptors for testing purposes.
 */
public final class MockUrlRequestJobFactory {

  private final CronetEngine mCronetEngine;

  /**
   * Sets up URL interceptors.
   */
  public MockUrlRequestJobFactory(CronetEngine cronetEngine) {
    mCronetEngine = cronetEngine;

    // Add a filter to immediately return a response
  }

  /**
   * Remove URL Interceptors.
   */
  public void shutdown() {
    // Remove the filter;
  }

  /**
   * Constructs a mock URL that hangs or fails at certain phase.
   *
   * @param phase at which request fails. It should be a value in
   *              org.chromium.net.test.FailurePhase.
   * @param netError reported by UrlRequestJob. Passing -1, results in hang.
   */
  public static String getMockUrlWithFailure(int phase, int netError) {
    assertTrue(netError < 0);
    switch (phase) {
    case FailurePhase.START:
    case FailurePhase.READ_SYNC:
    case FailurePhase.READ_ASYNC:
      break;
    default:
      throw new IllegalArgumentException("phase not in org.chromium.net.test.FailurePhase");
    }
    return "To be implemented";
  }

  /**
   * Constructs a mock URL that synchronously responds with data repeated many
   * times.
   *
   * @param data to return in response.
   * @param dataRepeatCount number of times to repeat the data.
   */
  public static String getMockUrlForData(String data, int dataRepeatCount) {
    return "To be implemented";
  }

  /**
   * Constructs a mock URL that will request client certificate and return
   * the string "data" as the response.
   */
  public static String getMockUrlForClientCertificateRequest() { return "To be implemented"; }

  /**
   * Constructs a mock URL that will fail with an SSL certificate error.
   */
  public static String getMockUrlForSSLCertificateError() { return "To be implemented"; }
}
