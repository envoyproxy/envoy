#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/access_log/access_log_config.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.validate.h"

#include "source/common/common/utility.h"
#include "source/common/formatter/substitution_format_string.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

template <class FormatterContext>
class FileAccessLog : public AccessLog::InstanceBase<FormatterContext> {
public:
  FileAccessLog(const Filesystem::FilePathAndType& access_log_file_info,
                AccessLog::FilterBasePtr<FormatterContext>&& filter,
                Formatter::FormatterBasePtr<FormatterContext>&& formatter,
                AccessLog::AccessLogManager& log_manager)
      : filter_(std::move(filter)), formatter_(std::move(formatter)) {
    log_file_ = log_manager.createAccessLog(access_log_file_info);
  }

  void log(const FormatterContext& context, const StreamInfo::StreamInfo& stream_info) override {
    if (filter_ != nullptr && !filter_->evaluate(context, stream_info)) {
      return;
    }
    log_file_->write(formatter_->format(context, stream_info));
  }

private:
  AccessLog::AccessLogFileSharedPtr log_file_;
  AccessLog::FilterBasePtr<FormatterContext> filter_;
  Formatter::FormatterBasePtr<FormatterContext> formatter_;
};

template <class FormatterContext>
class FileAccessLogFactory : public AccessLog::AccessLogInstanceFactoryBase<FormatterContext> {
public:
  FileAccessLogFactory()
      : name_(fmt::format("envoy.{}.access_loggers.file", FormatterContext::category())) {}

  AccessLog::InstanceBaseSharedPtr<FormatterContext>
  createAccessLogInstance(const Protobuf::Message& config,
                          AccessLog::FilterBasePtr<FormatterContext>&& filter,
                          Server::Configuration::CommonFactoryContext& context) override {
    const auto& typed_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::access_loggers::file::v3::FileAccessLog&>(
        config, context.messageValidationVisitor());

    Formatter::FormatterBasePtr<FormatterContext> formatter;

    switch (typed_config.access_log_format_case()) {
    case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::kFormat:
      if (typed_config.format().empty()) {
        formatter = getDefaultFormatter();
        if (formatter == nullptr) {
          ExceptionUtil::throwEnvoyException(
              "Access log: no format and no default format for file access log");
        }
      } else {
        envoy::config::core::v3::SubstitutionFormatString sff_config;
        sff_config.mutable_text_format_source()->set_inline_string(typed_config.format());
        formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig<FormatterContext>(
            sff_config, context);
      }
      break;
    case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
        kJsonFormat:
      formatter = Formatter::SubstitutionFormatStringUtils::createJsonFormatter<FormatterContext>(
          typed_config.json_format(), false, false);
      break;
    case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
        kTypedJsonFormat: {
      envoy::config::core::v3::SubstitutionFormatString sff_config;
      *sff_config.mutable_json_format() = typed_config.typed_json_format();
      formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig<FormatterContext>(
          sff_config, context);
      break;
    }
    case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
        kLogFormat:
      formatter = Formatter::SubstitutionFormatStringUtils::fromProtoConfig<FormatterContext>(
          typed_config.log_format(), context);
      break;
    case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
        ACCESS_LOG_FORMAT_NOT_SET:
      formatter = getDefaultFormatter();
      if (formatter == nullptr) {
        ExceptionUtil::throwEnvoyException(
            "Access log: no format and no default format for file access log");
      }
      break;
    }

    Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, typed_config.path()};
    return std::make_shared<FileAccessLog<FormatterContext>>(
        file_info, std::move(filter), std::move(formatter), context.accessLogManager());
  }

  AccessLog::InstanceBaseSharedPtr<FormatterContext> createAccessLogInstance(
      const Protobuf::Message& config, AccessLog::FilterBasePtr<FormatterContext>&& filter,
      Server::Configuration::ListenerAccessLogFactoryContext& context) override {
    return createAccessLogInstance(
        config, std::move(filter),
        static_cast<Server::Configuration::CommonFactoryContext&>(context));
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::access_loggers::file::v3::FileAccessLog()};
  }

  std::string name() const override { return name_; }

protected:
  virtual Formatter::FormatterBasePtr<FormatterContext> getDefaultFormatter() const {
    return nullptr;
  }

private:
  const std::string name_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
