#pragma once

#include <iomanip>
#include <sstream>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/common/time.h"

#include "source/common/common/assert.h"

#include "absl/status/statusor.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "openssl/bn.h"
#include "openssl/bytestring.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace Ocsp {

constexpr absl::string_view GENERALIZED_TIME_FORMAT = "%E4Y%m%d%H%M%S";

/**
 * Construct a `T` from the data contained in the CBS&. Functions
 * of this type must advance the input CBS& over the element.
 */
template <typename T> using Asn1ParsingFunc = std::function<absl::StatusOr<T>(CBS&)>;

/**
 * Utility functions for parsing DER-encoded `ASN.1` objects.
 * This relies heavily on the 'openssl/bytestring' API which
 * is BoringSSL's recommended interface for parsing DER-encoded
 * `ASN.1` data when there is not an existing wrapper.
 * This is not a complete library for `ASN.1` parsing and primarily
 * serves as abstractions for the OCSP module, but can be
 * extended and moved into a general utility to support parsing of
 * additional `ASN.1` objects.
 *
 * Each function adheres to the invariant that given a reference
 * to a crypto `bytestring` (CBS&), it will parse the specified
 * `ASN.1` element and advance `cbs` over it.
 *
 * An exception is thrown if the `bytestring` is malformed or does
 * not match the specified `ASN.1` object. The position
 * of `cbs` is not reliable after an exception is thrown.
 */
class Asn1Utility {
public:
  ~Asn1Utility() = default;

  /**
   * Extracts the full contents of `cbs` as a string.
   *
   * @param `cbs` a CBS& that refers to the current document position
   * @returns absl::string_view containing the contents of `cbs`
   */
  static absl::string_view cbsToString(CBS& cbs);

  /**
   * Parses all elements of an `ASN.1` SEQUENCE OF. `parse_element` must
   * advance its input CBS& over the entire element.
   *
   * @param cbs a CBS& that refers to an `ASN.1` SEQUENCE OF object
   * @param parse_element an `Asn1ParsingFunc<T>` used to parse each element
   * @returns absl::StatusOr<std::vector<T>> containing the parsed elements of the sequence
   * or an error string if `cbs` does not point to a well-formed
   * SEQUENCE OF object.
   */
  template <typename T>
  static absl::StatusOr<std::vector<T>> parseSequenceOf(CBS& cbs, Asn1ParsingFunc<T> parse_element);

  /**
   * Checks if an explicitly tagged optional element of `tag` is present and
   * if so parses its value with `parse_data`. If the element is not present,
   * `cbs` is not advanced.
   *
   * @param cbs a CBS& that refers to the current document position
   * @param parse_data an `Asn1ParsingFunc<T>` used to parse the data if present
   * @return absl::StatusOr<absl::optional<T>> with a `T` if `cbs` is of the specified tag,
   * nullopt if the element has a different tag, or an error string if parsing fails.
   */
  template <typename T>
  static absl::StatusOr<absl::optional<T>> parseOptional(CBS& cbs, Asn1ParsingFunc<T> parse_data,
                                                         unsigned tag);

  /**
   * Returns whether or not an element explicitly tagged with `tag` is present
   * at `cbs`. If so, `cbs` is advanced over the optional and assigns
   * `data` to the inner element, if `data` is not nullptr.
   * If `cbs` does not contain `tag`, `cbs` remains at the same position.
   *
   * @param cbs a CBS& that refers to the current document position
   * @param an unsigned explicit tag indicating an optional value
   *
   * @returns absl::StatusOr<bool> whether `cbs` points to an element tagged with `tag` or
   * an error string if parsing fails.
   */
  static absl::StatusOr<absl::optional<CBS>> getOptional(CBS& cbs, unsigned tag);

  /**
   * @param cbs a CBS& that refers to an `ASN.1` OBJECT IDENTIFIER element
   * @returns absl::StatusOr<std::string> the `OID` encoded in `cbs` or an error
   * string if `cbs` does not point to a well-formed OBJECT IDENTIFIER
   */
  static absl::StatusOr<std::string> parseOid(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an `ASN.1` `GENERALIZEDTIME` element
   * @returns absl::StatusOr<Envoy::SystemTime> the UTC timestamp encoded in `cbs`
   * or an error string if `cbs` does not point to a well-formed
   * `GENERALIZEDTIME`
   */
  static absl::StatusOr<Envoy::SystemTime> parseGeneralizedTime(CBS& cbs);

  /**
   * Parses an `ASN.1` INTEGER type into its hex string representation.
   * `ASN.1` INTEGER types are arbitrary precision.
   * If you're SURE the integer fits into a fixed-size int,
   * use `CBS_get_asn1_*` functions for the given integer type instead.
   *
   * @param cbs a CBS& that refers to an `ASN.1` INTEGER element
   * @returns absl::StatusOr<std::string> a hex representation of the integer
   * or an error string if `cbs` does not point to a well-formed INTEGER
   */
  static absl::StatusOr<std::string> parseInteger(CBS& cbs);

  /**
   * @param cbs a CBS& that refers to an `ASN.1` `OCTETSTRING` element
   * @returns absl::StatusOr<std::vector<uint8_t>> the octets in `cbs` or
   * an error string if `cbs` does not point to a well-formed `OCTETSTRING`
   */
  static absl::StatusOr<std::vector<uint8_t>> parseOctetString(CBS& cbs);

  /**
   * Advance `cbs` over an `ASN.1` value of the class `tag` if that
   * value is present. Otherwise, `cbs` stays in the same position.
   *
   * @param cbs a CBS& that refers to the current document position
   * @param tag the tag of the value to skip
   * @returns `absl::StatusOr<absl::monostate>` a unit type denoting success
   * or an error string if parsing fails.
   */
  static absl::StatusOr<absl::monostate> skipOptional(CBS& cbs, unsigned tag);

  /**
   * Advance `cbs` over an `ASN.1` value of the class `tag`.
   *
   * @param cbs a CBS& that refers to the current document position
   * @param tag the tag of the value to skip
   * @returns `absl::StatusOr<absl::monostate>` a unit type denoting success
   * or an error string if parsing fails.
   */
  static absl::StatusOr<absl::monostate> skip(CBS& cbs, unsigned tag);
};

template <typename T>
absl::StatusOr<std::vector<T>> Asn1Utility::parseSequenceOf(CBS& cbs,
                                                            Asn1ParsingFunc<T> parse_element) {
  CBS seq_elem;
  std::vector<T> vec;

  // Initialize seq_elem to first element in sequence.
  if (!CBS_get_asn1(&cbs, &seq_elem, CBS_ASN1_SEQUENCE)) {
    return absl::InvalidArgumentError("Expected sequence of ASN.1 elements.");
  }

  while (CBS_data(&seq_elem) < CBS_data(&cbs)) {
    // parse_element MUST advance seq_elem
    auto elem_res = parse_element(seq_elem);
    if (!elem_res.status().ok()) {
      return elem_res.status();
    }
    vec.push_back(elem_res.value());
  }

  RELEASE_ASSERT(CBS_data(&cbs) == CBS_data(&seq_elem),
                 "Sequence tag length must match actual length or element parsing would fail");

  return vec;
}

template <typename T>
absl::StatusOr<absl::optional<T>>
Asn1Utility::parseOptional(CBS& cbs, Asn1ParsingFunc<T> parse_data, unsigned tag) {
  auto maybe_data_res = getOptional(cbs, tag);

  if (!maybe_data_res.status().ok()) {
    return maybe_data_res.status();
  }

  auto maybe_data = maybe_data_res.value();
  if (maybe_data) {
    return parse_data(maybe_data.value());
  }

  return absl::nullopt;
}

} // namespace Ocsp
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
