#pragma once

#include <string>

namespace Zipkin {

class ZipkinCoreConstants {
public:
  static const std::string CLIENT_SEND;
  static const std::string CLIENT_RECV;
  static const std::string SERVER_SEND;
  static const std::string SERVER_RECV;
  static const std::string WIRE_SEND;
  static const std::string WIRE_RECV;
  static const std::string CLIENT_SEND_FRAGMENT;
  static const std::string CLIENT_RECV_FRAGMENT;
  static const std::string SERVER_SEND_FRAGMENT;
  static const std::string SERVER_RECV_FRAGMENT;
  static const std::string HTTP_HOST;
  static const std::string HTTP_METHOD;
  static const std::string HTTP_PATH;
  static const std::string HTTP_URL;
  static const std::string HTTP_STATUS_CODE;
  static const std::string HTTP_REQUEST_SIZE;
  static const std::string HTTP_RESPONSE_SIZE;
  static const std::string LOCAL_COMPONENT;
  static const std::string ERROR;
  static const std::string CLIENT_ADDR;
  static const std::string SERVER_ADDR;

  // Zipkin B3 Headers
  static const std::string X_B3_TRACE_ID;
  static const std::string X_B3_SPAN_ID;
  static const std::string X_B3_PARENT_SPAN_ID;
  static const std::string X_B3_SAMPLED;
  static const std::string X_B3_FLAGS;
};
} // Zipkin
