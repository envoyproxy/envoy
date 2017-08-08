#pragma once

namespace Envoy {
namespace Http {

/**
 * HTTP response codes.
 * http://www.iana.org/assignments/http-status-codes/http-status-codes.xhtml
 */
enum class Code {
  // clang-format off
  Continue                      = 100,

  OK                            = 200,
  Created                       = 201,
  Accepted                      = 202,
  NonAuthoritativeInformation   = 203,
  NoContent                     = 204,
  ResetContent                  = 205,
  PartialContent                = 206,
  MultiStatus                   = 207,
  AlreadyReported               = 208,
  IMUsed                        = 226,

  MultipleChoices               = 300,
  MovedPermanently              = 301,
  Found                         = 302,
  SeeOther                      = 303,
  NotModified                   = 304,
  UseProxy                      = 305,
  TemporaryRedirect             = 307,
  PermanentRedirect             = 308,

  BadRequest                    = 400,
  Unauthorized                  = 401,
  PaymentRequired               = 402,
  Forbidden                     = 403,
  NotFound                      = 404,
  MethodNotAllowed              = 405,
  NotAcceptable                 = 406,
  ProxyAuthenticationRequired   = 407,
  RequestTimeout                = 408,
  Conflict                      = 409,
  Gone                          = 410,
  LengthRequired                = 411,
  PreconditionFailed            = 412,
  PayloadTooLarge               = 413,
  URITooLong                    = 414,
  UnsupportedMediaType          = 415,
  RangeNotSatisfiable           = 416,
  ExpectationFailed             = 417,
  MisdirectedRequest            = 421,
  UnprocessableEntity           = 422,
  Locked                        = 423,
  FailedDependency              = 424,
  UpgradeRequired               = 426,
  PreconditionRequired          = 428,
  TooManyRequests               = 429,
  RequestHeaderFieldsTooLarge   = 431,

  InternalServerError           = 500,
  NotImplemented                = 501,
  BadGateway                    = 502,
  ServiceUnavailable            = 503,
  GatewayTimeout                = 504,
  HTTPVersionNotSupported       = 505,
  VariantAlsoNegotiates         = 506,
  InsufficientStorage           = 507,
  LoopDetected                  = 508,
  NotExtended                   = 510,
  NetworkAuthenticationRequired = 511
  // clang-format on
};

} // namespace Http
} // namespace Envoy
