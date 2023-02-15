package org.chromium.net.testing;

import static java.net.HttpURLConnection.HTTP_MOVED_TEMP;
import static java.net.HttpURLConnection.HTTP_NOT_FOUND;
import static java.net.HttpURLConnection.HTTP_OK;

import android.content.Context;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileReader;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicReference;
import okhttp3.mockwebserver.Dispatcher;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import okio.Buffer;

/**
 * Wrapper class to start an in-process native test server, and get URLs needed to talk to it.
 */
public final class NativeTestServer {

  // This variable contains the response body of a request to getSuccessURL().
  public static final String SUCCESS_BODY = "this is a text file\n";

  private static final String TEST_DATA_DIRECTORY = "test/java/org/chromium/net/testing/data";

  private static final String ECHO_HEADER_PATH = "/echo_header";
  private static final String ECHO_METHOD_PATH = "/echo_method";
  private static final String ECHO_ALL_HEADERS_PATH = "/echo_all_headers";
  private static final String REDIRECT_TO_ECHO_BODY_PATH = "/redirect_to_echo_body";
  private static final String ECHO_BODY_PATH = "/echo_body";

  private static final AtomicReference<MockWebServer> mockWebServerRef = new AtomicReference<>();

  public static boolean startNativeTestServer(Context context) {
    MockWebServer mockWebServer = new MockWebServer();
    try {
      mockWebServer.start();
      if (!mockWebServerRef.compareAndSet(null, mockWebServer)) {
        mockWebServer.shutdown(); // There is one already running - this new one is not needed.
        return false;
      }
    } catch (IOException e) {
      throw new IllegalStateException(e);
    }
    mockWebServer.setDispatcher(new MockWebServerDispatcher());
    return true;
  }

  public static void shutdownNativeTestServer() {
    MockWebServer mockWebServer = mockWebServerRef.getAndSet(null);
    if (mockWebServer != null) {
      try {
        mockWebServer.shutdown();
      } catch (IOException e) {
        throw new IllegalStateException(e);
      }
    }
  }

  public static String getEchoBodyURL() { return getUrl(ECHO_BODY_PATH); }

  public static String getEchoHeaderURL(String header) {
    return getUrl(ECHO_HEADER_PATH + "?" + header);
  }

  public static String getEchoAllHeadersURL() { return getUrl(ECHO_ALL_HEADERS_PATH); }

  public static String getEchoMethodURL() { return getUrl(ECHO_METHOD_PATH); }

  public static String getRedirectToEchoBody() { return getUrl(REDIRECT_TO_ECHO_BODY_PATH); }

  public static String getFileURL(String filePath) { return getUrl(filePath); }

  // The following URLs will make NativeTestServer serve a response based on
  // the contents of the corresponding file and its mock-http-headers file.

  public static String getSuccessURL() { return getUrl("/success.txt"); }

  public static String getInternalErrorURL() { return getUrl("/internalerror.txt"); }

  public static String getRedirectURL() { return getUrl("/redirect.html"); }

  public static String getMultiRedirectURL() { return getUrl("/multiredirect.html"); }

  public static String getNotFoundURL() { return getUrl("/notfound.html"); }

  public static int getPort() { return mockWebServerRef.get().getPort(); }

  public static String getHostPort() { return mockWebServerRef.get().getHostName(); }

  private static String getUrl(String path) { return mockWebServerRef.get().url(path).toString(); }

  private NativeTestServer() {}

  private static class MockWebServerDispatcher extends Dispatcher {

    @Override
    public MockResponse dispatch(RecordedRequest recordedRequest) {
      String path = recordedRequest.getRequestUrl().uri().getPath();
      switch (path) {
      case ECHO_HEADER_PATH:
        String headerValue = recordedRequest.getHeader(recordedRequest.getRequestUrl().query());
        return okTextResponse(headerValue != null ? headerValue : "Header not found. :(");

      case ECHO_METHOD_PATH:
        return okTextResponse(recordedRequest.getMethod());

      case ECHO_ALL_HEADERS_PATH:
        return okTextResponse(recordedRequest.getHeaders().toString());

      case REDIRECT_TO_ECHO_BODY_PATH:
        return new MockResponse()
            .addHeader("Location", ECHO_BODY_PATH)
            .setResponseCode(HTTP_MOVED_TEMP);

      case ECHO_BODY_PATH:
        return okTextResponse("POST".equals(recordedRequest.getMethod()) ||
                                      "PUT".equals(recordedRequest.getMethod())
                                  ? recordedRequest.getBody().readUtf8()
                                  : "Request has no body. :(");

      default:
        File bodyFile = new File(TEST_DATA_DIRECTORY + path);
        if (!bodyFile.exists()) {
          return new MockResponse().setResponseCode(HTTP_NOT_FOUND);
        }
        try {
          File headerFile = new File(TEST_DATA_DIRECTORY + path + ".mock-http-headers");
          List<String> lines = readLines(headerFile);
          MockResponse mockResponse = new MockResponse().setBody(readBuffer(bodyFile));
          for (String headerLine : lines.subList(1, lines.size())) {
            mockResponse.addHeader(headerLine);
          }
          return mockResponse.setStatus(lines.get(0));
        } catch (Exception e) {
          e.printStackTrace();
          return new MockResponse().setResponseCode(HTTP_NOT_FOUND);
        }
      }
    }

    private static List<String> readLines(File file) {
      List<String> lines = new ArrayList<>();
      try (BufferedReader br = new BufferedReader(new FileReader(file))) {
        String line;
        while ((line = br.readLine()) != null) {
          lines.add(line);
        }
      } catch (IOException e) {
        throw new IllegalStateException(e);
      }
      return lines;
    }

    private static Buffer readBuffer(File file) {
      try {
        return new Buffer().readFrom(new FileInputStream(file));
      } catch (IOException e) {
        throw new IllegalStateException(e);
      }
    }

    private static MockResponse okTextResponse(String text) {
      return new MockResponse()
          .addHeader("Connection", "close")
          .setBody(text)
          .setHeader("Content-Type", "text/plain")
          .setResponseCode(HTTP_OK);
    }
  }
}
