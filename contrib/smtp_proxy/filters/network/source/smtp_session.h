#pragma once
#include <cstdint>

#include "source/common/common/logger.h"

#include "contrib/smtp_proxy/filters/network/source/smtp_command.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_handler.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_transaction.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpSession : public SmtpHandler {
public:
  enum class State {
    ConnectionRequest = 0,
    ConnectionSuccess = 1,
    SessionInitRequest = 2,
    SessionInProgress = 3,
    SessionTerminationRequest = 4,
    SessionTerminated = 5,
    UpstreamTlsNegotiation = 6,
    DownstreamTlsNegotiation = 7,
    SessionAuthRequest = 8,
    SessionResetRequest = 9,
    XReqIdTransfer = 10,
  };

  struct SmtpSessionStats {
    int transactions_failed;
    int transactions_completed;
    int transactions_aborted;
    int total_transactions;
    int total_commands;
  };

  SmtpSession(DecoderCallbacks* callbacks, TimeSource& time_source,
              Random::RandomGenerator& random_generator);

  ~SmtpSession() {
    delete smtp_transaction_;
    smtp_transaction_ = nullptr;
  }

  void setState(SmtpSession::State state) { state_ = state; }
  SmtpSession::State getState() { return state_; }

  SmtpTransaction* getTransaction() { return smtp_transaction_; }
  void createNewTransaction();
  void endTransaction();

  void setTransactionState(SmtpTransaction::State state) { smtp_transaction_->setState(state); };
  SmtpTransaction::State getTransactionState() { return smtp_transaction_->getState(); }

  SmtpSession::SmtpSessionStats& getSessionStats() { return session_stats_; }

  void setSessionEncrypted(bool flag) { session_encrypted_ = flag; }
  bool isSessionEncrypted() const { return session_encrypted_; }

  void encode(ProtobufWkt::Struct& metadata);

  SmtpUtils::Result handleCommand(std::string& command, std::string& args) override;

  SmtpUtils::Result handleEhlo(std::string& command);
  SmtpUtils::Result handleMail(std::string& args);
  SmtpUtils::Result handleRcpt(std::string& args);
  SmtpUtils::Result handleData(std::string& args);
  SmtpUtils::Result handleReset(std::string& args);
  SmtpUtils::Result handleQuit(std::string& args);
  SmtpUtils::Result handleAuth();
  SmtpUtils::Result handleStarttls();
  SmtpUtils::Result handleOtherCmds(std::string& args);

  SmtpUtils::Result handleResponse(uint16_t& response_code, std::string& response) override;
  SmtpUtils::Result handleConnResponse(uint16_t& response_code, std::string& response);
  SmtpUtils::Result handleEhloResponse(uint16_t& response_code, std::string& response);
  SmtpUtils::Result handleMailResponse(uint16_t& response_code, std::string& response);
  SmtpUtils::Result handleRcptResponse(uint16_t& response_code, std::string& response);
  SmtpUtils::Result handleDataResponse(uint16_t& response_code, std::string& response);
  SmtpUtils::Result handleResetResponse(uint16_t& response_code, std::string& response);
  SmtpUtils::Result handleQuitResponse(uint16_t& response_code, std::string& response);
  SmtpUtils::Result handleAuthResponse(uint16_t& response_code, std::string& response);
  SmtpUtils::Result handleStarttlsResponse(uint16_t& response_code, std::string& response);
  SmtpUtils::Result handleXReqIdResponse(uint16_t& response_code, std::string& response);
  SmtpUtils::Result handleOtherResponse(uint16_t& response_code, std::string& response);

  void abortTransaction();
  void handleDownstreamTls();

  void newCommand(const std::string& name, SmtpCommand::Type type);
  SmtpUtils::Result storeResponse(std::string response, uint16_t response_code);
  std::string& getResponseOnHold() { return response_on_hold_; }
  void setResponseOnHold(std::string& resp) { response_on_hold_ = resp; }
  bool isDataTransferInProgress() override { return data_transfer_in_progress_; }
  bool isTerminated() override { return state_ == State::SessionTerminated; }
  void terminateSession();
  void setDataTransferInProgress(bool status) { data_transfer_in_progress_ = status; }
  bool isCommandInProgress() override { return command_in_progress_; }

  bool isAuthenticated() { return auth_complete_; }
  void setAuthStatus(bool status) { auth_complete_ = status; }

  std::shared_ptr<SmtpCommand> getCurrentCommand() { return current_command_; }
  void updateBytesMeterOnCommand(Buffer::Instance& data) override;
  void updateBytesMeterOnResponse(Buffer::Instance& data);

  void setSessionMetadata();
  void onTransactionComplete();

  bool isXReqIdSent() { return x_req_id_sent_; }

private:
  std::string session_id_;
  SmtpSession::State state_{State::ConnectionRequest};
  SmtpTransaction* smtp_transaction_{};
  SmtpSession::SmtpSessionStats session_stats_ = {};
  bool session_encrypted_{false}; // tells if smtp session is encrypted
  DecoderCallbacks* callbacks_{};
  TimeSource& time_source_;
  Random::RandomGenerator& random_generator_;
  std::shared_ptr<SmtpCommand> current_command_;
  std::vector<std::shared_ptr<SmtpCommand>> session_commands_;
  std::string response_on_hold_;
  bool data_transfer_in_progress_{false};
  bool transaction_in_progress_{false};
  bool command_in_progress_{false};
  bool auth_complete_{false};
  bool x_req_id_sent_{false};
  SmtpUtils::SessionType upstream_session_type_{SmtpUtils::SessionType::PlainText};
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
