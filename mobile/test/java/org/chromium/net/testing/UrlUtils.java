package org.chromium.net.testing;

import org.junit.Assert;

/**
 * Collection of URL utilities.
 */
public final class UrlUtils {

  /**
   * Construct the full path of a test data file.
   * @param path Pathname relative to external/
   */
  public static String getIsolatedTestFilePath(String path) {
    return getIsolatedTestRoot() + "/" + path;
  }

  /**
   * Returns the root of the test data directory.
   */
  public static String getIsolatedTestRoot() {
    try (StrictModeContext ignored = StrictModeContext.allowDiskReads()) {
      return PathUtils.getExternalStorageDirectory() + "/chromium_tests_root";
    }
  }

  /**
   * Construct a data:text/html URI for loading from an inline HTML.
   * @param html An unencoded HTML
   * @return String An URI that contains the given HTML
   */
  public static String encodeHtmlDataUri(String html) {
    try {
      // URLEncoder encodes into application/x-www-form-encoded, so
      // ' '->'+' needs to be undone and replaced with ' '->'%20'
      // to match the Data URI requirements.
      String encoded = "data:text/html;utf-8," + java.net.URLEncoder.encode(html, "UTF-8");
      encoded = encoded.replace("+", "%20");
      return encoded;
    } catch (java.io.UnsupportedEncodingException e) {
      Assert.fail("Unsupported encoding: " + e.getMessage());
      return null;
    }
  }

  private UrlUtils() {}
}
