

#include <iostream>
#include <string>

namespace llhttp {
#include <llhttp.h>

void parseWithLlhttp(const std::string& message) {
  std::cout << "parsing with llhttp" << std::endl;
  llhttp_t parser;
  llhttp_settings_t settings;

  /* Initialize user callbacks and settings */
  llhttp_settings_init(&settings);

  /* Set user callback */
  settings.on_message_complete = [](llhttp_t*) -> int {
    std::cout << "callback called" << std::endl;
    return 0;
  };

  settings.on_url = [](llhttp_t*, const char*, size_t) -> int {
    std::cout << "on_url called" << std::endl;
    return 0;
  };

  settings.on_header_field = [](llhttp_t*, const char* at, size_t) -> int {
    std::cout << "found header field: " << at << std::endl;
    return 0;
  };

  settings.on_header_value = [](llhttp_t*, const char* at, size_t) -> int {
    std::cout << "found header value: " << at << std::endl;
    return 0;
  };

  settings.on_headers_complete = [](llhttp_t* parser) -> int {
    std::cout << "headers_complete called, status: " << parser->status_code << std::endl;
    return 1;
  };

  settings.on_body = [](llhttp_t*, const char* at, size_t) -> int {
    std::cout << "body: " << at << std::endl;
    return 0;
  };

  /* Initialize the parser in HTTP_BOTH mode, meaning that it will select between
  * HTTP_REQUEST and HTTP_RESPONSE parsing automatically while reading the first
  * input.
  */
  llhttp_init(&parser, HTTP_RESPONSE, &settings);

  /* Parse request! */
  const int message_len = message.size();

  llhttp_resume(&parser);

  enum llhttp_errno err = llhttp_execute(&parser, message.c_str(), message_len);
  std::cerr <<  "Parse error 1: " << llhttp_errno_name(err) << std::endl;;
  std::cerr <<  "Parse error 2: " << llhttp_errno_name(llhttp_get_errno(&parser)) << std::endl;;
}

} // namespace llhttp

namespace http_parser {

#include <http_parser.h>

void parseWithHttpParser(const std::string& message) {
  std::cout << "parsing with http-parser" << std::endl;
  http_parser parser;
  http_parser_settings settings;

  /* Initialize user callbacks and settings */
  http_parser_settings_init(&settings);

  /* Set user callback */
  settings.on_message_complete = [](http_parser*) -> int {
    std::cout << "callback called" << std::endl;
    return 0;
  };

  settings.on_url = [](http_parser*, const char*, size_t) -> int {
    std::cout << "on_url called" << std::endl;
    return 0;
  };

  settings.on_header_field = [](http_parser*, const char* at, size_t) -> int {
    std::cout << "found header field: " << at << std::endl;
    return 0;
  };

  settings.on_header_value = [](http_parser*, const char* at, size_t) -> int {
    std::cout << "found header value: " << at << std::endl;
    return 0;
  };

  settings.on_headers_complete = [](http_parser* parser) -> int {
    std::cout << "headers_complete called, status: " << parser->status_code << std::endl;
    return 0;
  };

  settings.on_body = [](http_parser*, const char* at, size_t) -> int {
    std::cout << "body: " << at << std::endl;
    return 0;
  };

  /* Initialize the parser in HTTP_BOTH mode, meaning that it will select between
  * HTTP_REQUEST and HTTP_RESPONSE parsing automatically while reading the first
  * input.
  */
  http_parser_init(&parser, HTTP_RESPONSE);

  /* Parse request! */
  const int message_len = message.size();

  auto bytes = http_parser_execute(&parser, &settings, message.c_str(), message_len);
  std::cout << "bytes parsed: " << bytes << std::endl;
  std::cerr <<  "Parse error: " << std::string(http_errno_name(HTTP_PARSER_ERRNO(&parser))) << std::endl;;
}
}

int main(int, char**) {
  llhttp::parseWithLlhttp("HTTP/1.1 200 OK\r\n\r\nHello World");
  http_parser::parseWithHttpParser("HTTP/1.1 200 OK\r\n\r\nHello World");

  std::cout << "================================================================" << std::endl;

  llhttp::parseWithLlhttp("HTTP/1.1 200 OK\r\n\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello World\r\n0\r\nhello: world\r\nsecond: header\r\n\r\n");
  http_parser::parseWithHttpParser("HTTP/1.1 200 OK\r\n\r\ntransfer-encoding: chunked\r\n\r\nb\r\nHello World\r\n0\r\nhello: world\r\nsecond: header\r\n\r\n");

  std::cout << "================================================================" << std::endl;
}
