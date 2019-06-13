package io.envoyproxy.envoymobile.helloenvoy;

// Response is a class to handle HTTP responses.
public class Response {
  public final String title;
  public final String header;

  public Response(String title, String header) {
    this.title = title;
    this.header = header;
  }
}
