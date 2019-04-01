#include "extensions/tracers/xray/sampling.h"

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <cstdint>
#include <fstream>
#include <sstream>

#include "common/http/exception.h"

#include "extensions/tracers/xray/reservoir.h"
#include "extensions/tracers/xray/util.h"

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

SamplingRule::SamplingRule(int fixed_target, float rate, Reservoir& reservoir)
    : fixed_target_(fixed_target), rate_(rate), reservoir_(reservoir) {}

SamplingRule::SamplingRule(std::string& host_name, std::string& service_name,
                           std::string& http_method, std::string& url_path, int fixed_target,
                           float rate, Reservoir& reservoir)
    : host_(host_name), service_name_(service_name), http_method_(http_method), url_path_(url_path),
      fixed_target_(fixed_target), rate_(rate), reservoir_(reservoir) {}

bool SamplingRule::appliesTo(std::string request_host, std::string request_path,
                             std::string request_method) {
  return (request_host.empty() || Util::wildcardMatch(host_, request_host)) &&
         (request_path.empty() || Util::wildcardMatch(url_path_, request_path)) &&
         (request_method.empty() || Util::wildcardMatch(http_method_, request_method));
}

void SamplingRuleManifest::deSerialize(std::string json_file) {
  std::stringstream ss;
  std::ifstream file(json_file);
  if (file) {
    ss << file.rdbuf();
    file.close();
  } else {
    throw EnvoyException("Unable to open custom sampling rule json file.");
  }

  rapidjson::Document document;
  if (document.Parse<0>(ss.str().c_str()).HasParseError())
    throw EnvoyException("Custom sampling rule json file parsing error.");

  if (document.HasMember("version")) {
    if (VERSION_ != document["version"].GetInt()) {
      throw EnvoyException("Invalid Custom Sampling Rule Json file: invalid version number");
    }
  } else {
    throw EnvoyException("Invalid Custom Sampling Rule Json file: missing version.");
  }

  if (document.HasMember("default") && document["default"].IsObject()) {
    const rapidjson::Value& default_rule = document["default"];
    SamplingRule default_sampling_rule;
    for (rapidjson::Value::ConstMemberIterator m = default_rule.MemberBegin();
         m != default_rule.MemberEnd(); ++m) {
      std::string key = m->name.GetString();
      if (key == "fixed_target" && m->value.IsInt()) {
        default_sampling_rule.setFixedTarget(m->value.GetInt());
      } else if (key == "rate" && m->value.IsFloat()) {
        default_sampling_rule.setFixedTarget(m->value.GetFloat());
      } else {
        throw EnvoyException(
            "Invalid Custom Sampling Rule Json file: default setting is not correct.");
      }
    }
    setDefaultRule(default_sampling_rule);
  } else {
    throw EnvoyException("Invalid Custom Sampling Rule Json file: missing default rule.");
  }

  if (document.HasMember("rules") && document["rules"].IsArray()) {
    std::vector<SamplingRule> custom_rules;
    const rapidjson::Value& rules = document["rules"];
    for (rapidjson::SizeType i = 0; i < rules.Size(); i++) {
      SamplingRule rule;
      for (rapidjson::Value::ConstMemberIterator m = rules[i].MemberBegin();
           m != rules[i].MemberEnd(); ++m) {
        std::string key = m->name.GetString();
        if (key == "description") {
          continue;
        } else if (key == "host" && m->value.IsString()) {
          rule.setHost(m->value.GetString());
        } else if (key == "http_method" && m->value.IsString()) {
          rule.setHttpMethod(m->value.GetString());
        } else if (key == "url_path" && m->value.IsString()) {
          rule.setUrlPath(m->value.GetString());
        } else if (key == "fixed_target" && m->value.IsInt()) {
          rule.setFixedTarget(m->value.GetInt());
        } else if (key == "rate" && m->value.IsFloat()) {
          rule.setRate(m->value.GetFloat());
        } else {
          throw EnvoyException(
              "Invalid Custom Sampling Rule Json file: rules setting is not correct.");
        }
      }
      custom_rules.push_back(rule);
    }
    setRules(custom_rules);
  } else {
    throw EnvoyException("Invalid Custom Sampling Rule Json file: missing rules.");
  }
}

LocalizedSamplingStrategy::LocalizedSamplingStrategy(std::string json_file, TimeSource& time_source)
    : time_source_(time_source) {
  if (json_file.empty()) {
    ENVOY_LOG(info, "unable to parse empty json file. falling back to default rule set.");
    std::vector<SamplingRule> empty_rules;
    Reservoir reservoir(1, time_source);
    SamplingRule default_rule(1, 0.05, reservoir);
    SamplingRuleManifest default_rule_manifest(SamplingRuleManifest::VERSION_, default_rule,
                                               empty_rules);
    setDefaultRuleManifest(default_rule_manifest);
  } else {
    // Parse the json file that customer give
    SamplingRuleManifest manifest;
    manifest.deSerialize(json_file);
    int fixed_target = manifest.defaultRule().fixedTarget();
    Reservoir default_rule_reservoir(fixed_target, time_source_);
    manifest.defaultRule().setReservoir(std::move(default_rule_reservoir));
    for (std::size_t i = 0; i < manifest.rules().size(); i++) {
      fixed_target = manifest.rules()[i].fixedTarget();
      Reservoir rule_reservoir(fixed_target, time_source_);
      manifest.rules()[i].setReservoir(std::move(rule_reservoir));
    }
    setCustomRuleManifest(manifest);
  }
}

bool LocalizedSamplingStrategy::shouldTrace(SamplingRequest request) {
  ENVOY_LOG(debug, "determining ShouldTrace decision for: host: {} path: {} method: {}",
            request.host(), request.httpUrl(), request.httpMethod());
  // Will implement custom rules matching if they are provided by customer.
  // Below shows the logic about rule matching.
  if (isSetCustomRuleManifest()) {
    if (!customRuleManifest().rules().empty()) {
      std::vector<SamplingRule> rules = customRuleManifest().rules();
      for (std::size_t i = 0; i < customRuleManifest().rules().size(); i++) {
        if (customRuleManifest().rules()[i].appliesTo(request.host(), request.httpUrl(),
                                                      request.httpMethod())) {
          return shouldTraceHelper(customRuleManifest().rules()[i]);
        }
      }
    }
  }

  return shouldTraceHelper(default_rule_manifest_.defaultRule());
}

bool LocalizedSamplingStrategy::shouldTraceHelper(SamplingRule& rule) {
  if (rule.isSetReservoir() && rule.reservoir().take()) {
    return true;
  } else {
    return Util::generateRandomDouble(time_source_) < rule.rate();
  }
}

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy