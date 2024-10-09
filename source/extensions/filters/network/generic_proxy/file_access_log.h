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

template <class Context> class FileAccessLogBase : public AccessLog::InstanceBase<Context> {
public:
  FileAccessLogBase(const Filesystem::FilePathAndType& access_log_file_info,
                    AccessLog::FilterBasePtr<Context>&& filter,
                    Formatter::FormatterBasePtr<Context>&& formatter,
                    AccessLog::AccessLogManager& log_manager)
      : filter_(std::move(filter)), formatter_(std::move(formatter)) {
    log_file_ = log_manager.createAccessLog(access_log_file_info).value();
  }

  void log(const Context& context, const StreamInfo::StreamInfo& stream_info) override {
    if (filter_ != nullptr && !filter_->evaluate(context, stream_info)) {
      return;
    }
    log_file_->write(formatter_->formatWithContext(context, stream_info));
  }

private:
  AccessLog::AccessLogFileSharedPtr log_file_;
  AccessLog::FilterBasePtr<Context> filter_;
  Formatter::FormatterBasePtr<Context> formatter_;
};

template <class Context>
class FileAccessLogFactoryBase : public AccessLog::AccessLogInstanceFactoryBase<Context> {
public:
  FileAccessLogFactoryBase()
      : name_(fmt::format("envoy.{}.access_loggers.file", Context::category())) {}

  AccessLog::InstanceBaseSharedPtr<Context>
  createAccessLogInstance(const Protobuf::Message& config,
                          AccessLog::FilterBasePtr<Context>&& filter,
                          Server::Configuration::FactoryContext& context) override {
    const auto& typed_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::access_loggers::file::v3::FileAccessLog&>(
        config, context.messageValidationVisitor());

    Formatter::FormatterBasePtr<Context> formatter;

    switch (typed_config.access_log_format_case()) {
    case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::kFormat:
      if (typed_config.format().empty()) {
        formatter = getDefaultFormatter();
      } else {
        envoy::config::core::v3::SubstitutionFormatString sff_config;
        sff_config.mutable_text_format_source()->set_inline_string(typed_config.format());
        formatter = THROW_OR_RETURN_VALUE(
            Formatter::SubstitutionFormatStringUtils::fromProtoConfig<Context>(sff_config, context),
            Formatter::FormatterBasePtr<Context>);
      }
      break;
    case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
        kJsonFormat:
      formatter = Formatter::SubstitutionFormatStringUtils::createJsonFormatter<Context>(
          typed_config.json_format(), false, false, false);
      break;
    case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
        kTypedJsonFormat: {
      envoy::config::core::v3::SubstitutionFormatString sff_config;
      *sff_config.mutable_json_format() = typed_config.typed_json_format();
      formatter = THROW_OR_RETURN_VALUE(
          Formatter::SubstitutionFormatStringUtils::fromProtoConfig<Context>(sff_config, context),
          Formatter::FormatterBasePtr<Context>);
      break;
    }
    case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
        kLogFormat:
      formatter =
          THROW_OR_RETURN_VALUE(Formatter::SubstitutionFormatStringUtils::fromProtoConfig<Context>(
                                    typed_config.log_format(), context),
                                Formatter::FormatterBasePtr<Context>);
      break;
    case envoy::extensions::access_loggers::file::v3::FileAccessLog::AccessLogFormatCase::
        ACCESS_LOG_FORMAT_NOT_SET:
      formatter = getDefaultFormatter();
      break;
    }
    if (formatter == nullptr) {
      ExceptionUtil::throwEnvoyException(
          "Access log: no format and no default format for file access log");
    }

    Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, typed_config.path()};
    return std::make_shared<FileAccessLogBase<Context>>(
        file_info, std::move(filter), std::move(formatter),
        context.serverFactoryContext().accessLogManager());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::access_loggers::file::v3::FileAccessLog()};
  }

  std::string name() const override { return name_; }

protected:
  virtual Formatter::FormatterBasePtr<Context> getDefaultFormatter() const { return nullptr; }

private:
  const std::string name_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
