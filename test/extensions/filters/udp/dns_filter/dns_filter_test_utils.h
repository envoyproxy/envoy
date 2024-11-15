#pragma once

#include "source/extensions/filters/udp/dns_filter/dns_filter.h"
#include "source/extensions/filters/udp/dns_filter/dns_filter_constants.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace DnsFilter {
namespace Utils {

static constexpr uint64_t MAX_UDP_DNS_SIZE{512};

std::string buildQueryFromBytes(const char* bytes, const size_t count);
std::string buildQueryForDomain(const std::string& name, uint16_t rec_type, uint16_t rec_class,
                                const uint16_t query_id = 0);
void verifyAddress(const std::list<std::string>& addresses, const DnsAnswerRecordPtr& answer);
size_t getResponseQueryCount(DnsMessageParser& parser);

/**
 * @brief Extracts the protocol name from the fully qualified service name. The leading underscore
 * is discarded from the output
 */
absl::string_view getProtoFromName(const absl::string_view name);

class DnsResponseValidator : public Logger::Loggable<Logger::Id::filter> {
public:
  struct DnsAnswerCtx {
    DnsAnswerCtx(const Buffer::InstancePtr& buffer, const absl::string_view record_name,
                 const uint16_t record_type, const uint16_t record_class,
                 const uint16_t available_bytes, const uint16_t data_length, const uint32_t ttl,
                 uint64_t& offset)
        : buffer_(buffer), record_name_(record_name), record_type_(record_type),
          record_class_(record_class), available_bytes_(available_bytes), data_length_(data_length),
          ttl_(ttl), offset_(offset) {}

    const Buffer::InstancePtr& buffer_;
    const std::string record_name_;
    const uint16_t record_type_;
    const uint16_t record_class_;
    const uint16_t available_bytes_;
    const uint16_t data_length_;
    const uint32_t ttl_;
    uint64_t& offset_;
  };

  /**
   * @brief Parse the response DNS message and create a context object
   *
   * @param client_request a structure containing addressing information and the buffer received
   * from a client
   */
  static DnsQueryContextPtr createResponseContext(Network::UdpRecvData& client_request,
                                                  DnsParserCounters& counters);

  /**
   * @param buffer a reference to the incoming DNS response object
   * @return bool true if all DNS records and flags were successfully parsed from the buffer
   */
  static bool validateDnsResponseObject(DnsQueryContextPtr& context,
                                        const Buffer::InstancePtr& buffer);

  /**
   * @brief Extracts a DNS query name from a buffer
   *
   * @param buffer the buffer from which the name is extracted
   * @param available_bytes the size of the remaining bytes in the buffer on which we can operate
   * @param name_offset the offset from which parsing begins and ends. The updated value is
   * returned to the caller
   */
  static const std::string parseDnsNameRecord(const Buffer::InstancePtr& buffer,
                                              uint64_t& available_bytes, uint64_t& name_offset);

  /**
   * @brief parse an A or AAAA DNS Record
   *
   * @param context the query context for which we are generating a response
   * @return DnsAnswerRecordPtr a pointer to a DnsAnswerRecord object containing the parsed
   answer
   * record
   */
  static DnsAnswerRecordPtr parseDnsARecord(DnsAnswerCtx& ctx);

  /**
   * @brief parse a Server Selection (SRV) DNS Record
   *
   * @param context the query context for which we are generating a response
   * @return DnsSrvRecordPtr a pointer to a DnsSrvRecord object containing the parsed server record
   */
  static DnsSrvRecordPtr parseDnsSrvRecord(DnsAnswerCtx& ctx);

  /**
   * @brief parse a single answer record from a client request or filter response
   *
   * @param buffer a reference to a buffer containing a DNS request or response
   * @param offset the buffer offset at which parsing is to begin. This parameter is updated when
   * one record is parsed from the buffer and returned to the caller.
   * @return DnsQueryRecordPtr a pointer to a DnsAnswerRecord object containing all query and
   * answer data parsed from the buffer
   */
  static DnsAnswerRecordPtr parseDnsAnswerRecord(const Buffer::InstancePtr& buffer,
                                                 uint64_t& offset);

  /**
   * @brief Parse answer records using a single function. Answer records follow a common format
   * so one function will suffice for reading them.
   *
   * @param answers a reference to the map containing the parsed records
   * @param answer_count the indicated number of records we expect parsed from the request header
   * @param buffer a reference to a buffer containing a DNS request or response
   * @param offset a reference to an index into the buffer indicating the position where reading may
   * begin
   */
  static bool parseAnswerRecords(DnsAnswerMap& answers, const uint16_t answer_count,
                                 const Buffer::InstancePtr& buffer, uint64_t& offset);

private:
  enum class DnsQueryParseState {
    Init,
    Flags,     // 2 bytes
    Questions, // 2 bytes
    Answers,   // 2 bytes
    Authority, // 2 bytes
    Authority2 // 2 bytes
  };
};

} // namespace Utils
} // namespace DnsFilter
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
