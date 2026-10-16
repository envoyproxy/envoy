// NOLINT(namespace-envoy)
#include <optional>

#include "envoy/registry/registry.h"

#include "contrib/istio/filters/common/source/metadata_object.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Istio {
namespace Common {

using Envoy::Protobuf::util::MessageDifferencer;

TEST(WorkloadMetadataObjectTest, AsString) {
  constexpr absl::string_view identity = "spiffe://cluster.local/ns/default/sa/default";
  ::Istio::Common::WorkloadMetadataObject deploy(
      "pod-foo-1234", "my-cluster", "default", "foo", "foo-service", "v1alpha3", "", "",
      ::Istio::Common::WorkloadType::Deployment, identity, "", "");

  ::Istio::Common::WorkloadMetadataObject pod("pod-foo-1234", "my-cluster", "default", "foo",
                                              "foo-service", "v1alpha3", "", "",
                                              ::Istio::Common::WorkloadType::Pod, identity, "", "");

  ::Istio::Common::WorkloadMetadataObject cronjob(
      "pod-foo-1234", "my-cluster", "default", "foo", "foo-service", "v1alpha3", "foo-app", "v1",
      ::Istio::Common::WorkloadType::CronJob, identity, "", "");

  ::Istio::Common::WorkloadMetadataObject job("pod-foo-1234", "my-cluster", "default", "foo",
                                              "foo-service", "v1alpha3", "", "",
                                              ::Istio::Common::WorkloadType::Job, identity, "", "");

  EXPECT_EQ(deploy.serializeAsString(),
            absl::StrCat("type=deployment,workload=foo,name=pod-foo-1234,cluster=my-cluster,",
                         "namespace=default,service=foo-service,revision=v1alpha3"));

  EXPECT_EQ(pod.serializeAsString(),
            absl::StrCat("type=pod,workload=foo,name=pod-foo-1234,cluster=my-cluster,",
                         "namespace=default,service=foo-service,revision=v1alpha3"));

  EXPECT_EQ(cronjob.serializeAsString(),
            absl::StrCat("type=cronjob,workload=foo,name=pod-foo-1234,cluster=my-cluster,",
                         "namespace=default,service=foo-service,revision=v1alpha3,",
                         "app=foo-app,version=v1"));

  EXPECT_EQ(job.serializeAsString(),
            absl::StrCat("type=job,workload=foo,name=pod-foo-1234,cluster=my-cluster,",
                         "namespace=default,service=foo-service,revision=v1alpha3"));
}

void checkStructConversion(const Envoy::StreamInfo::FilterState::Object& data) {
  const auto& obj = dynamic_cast<const ::Istio::Common::WorkloadMetadataObject&>(data);
  auto pb = ::Istio::Common::convertWorkloadMetadataToStruct(obj);
  auto obj2 = ::Istio::Common::convertStructToWorkloadMetadata(pb);
  EXPECT_EQ(obj2->serializeAsString(), obj.serializeAsString());
  MessageDifferencer::Equals(*(obj2->serializeAsProto()), *(obj.serializeAsProto()));
  EXPECT_EQ(obj2->hash(), obj.hash());
}

TEST(WorkloadMetadataObjectTest, ConversionWithLabels) {
  constexpr absl::string_view identity = "spiffe://cluster.local/ns/default/sa/default";
  ::Istio::Common::WorkloadMetadataObject deploy(
      "pod-foo-1234", "my-cluster", "default", "foo", "foo-service", "v1alpha3", "", "",
      ::Istio::Common::WorkloadType::Deployment, identity, "", "");
  deploy.setLabels({{"label1", "value1"}, {"label2", "value2"}});
  auto pb = ::Istio::Common::convertWorkloadMetadataToStruct(deploy);
  auto obj1 = ::Istio::Common::convertStructToWorkloadMetadata(pb, {"label1", "label2"});
  EXPECT_EQ(obj1->getLabels().size(), 2);
  auto obj2 = ::Istio::Common::convertStructToWorkloadMetadata(pb, {"label1"});
  EXPECT_EQ(obj2->getLabels().size(), 1);
  absl::flat_hash_set<std::string> empty;
  auto obj3 = ::Istio::Common::convertStructToWorkloadMetadata(pb, empty);
  EXPECT_EQ(obj3->getLabels().size(), 0);
}

TEST(WorkloadMetadataObjectTest, Conversion) {
  {
    constexpr absl::string_view identity = "spiffe://cluster.local/ns/default/sa/default";
    const auto r = ::Istio::Common::convertBaggageToWorkloadMetadata(
        "k8s.deployment.name=foo,k8s.cluster.name=my-cluster,"
        "k8s.namespace.name=default,service.name=foo-service,service.version=v1alpha3,app.name=foo-"
        "app,app.version=latest",
        identity);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("type")), ::Istio::Common::DeploymentSuffix);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("workload")), "foo");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("name")), "");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "default");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("cluster")), "my-cluster");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-app");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "latest");
    EXPECT_EQ(r->identity(), identity);
    checkStructConversion(*r);
  }

  {
    const auto r = ::Istio::Common::convertBaggageToWorkloadMetadata(
        "k8s.pod.name=foo-pod-435,k8s.cluster.name=my-cluster,k8s.namespace.name="
        "test,k8s.instance.name=foo-instance-435,service.name=foo-service,service.version=v1beta2");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1beta2");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("type")), ::Istio::Common::PodSuffix);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("workload")), "foo-pod-435");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("name")), "foo-instance-435");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "test");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("cluster")), "my-cluster");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "v1beta2");
    checkStructConversion(*r);
  }

  {
    const auto r = ::Istio::Common::convertBaggageToWorkloadMetadata(
        "k8s.job.name=foo-job-435,k8s.cluster.name=my-cluster,k8s.namespace.name="
        "test,k8s.instance.name=foo-instance-435,service.name=foo-service,service.version=v1beta4");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1beta4");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("type")), ::Istio::Common::JobSuffix);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("workload")), "foo-job-435");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("name")), "foo-instance-435");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "test");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("cluster")), "my-cluster");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "v1beta4");
    checkStructConversion(*r);
  }

  {
    const auto r = ::Istio::Common::convertBaggageToWorkloadMetadata(
        "k8s.cronjob.name=foo-cronjob,k8s.cluster.name=my-cluster,"
        "k8s.namespace.name=test,service.name=foo-service,service.version=v1beta4");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1beta4");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("type")), ::Istio::Common::CronJobSuffix);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("workload")), "foo-cronjob");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("name")), "");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "test");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("cluster")), "my-cluster");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "v1beta4");
    checkStructConversion(*r);
  }

  {
    const auto r = ::Istio::Common::convertBaggageToWorkloadMetadata(
        "k8s.deployment.name=foo,k8s.namespace.name=default,"
        "service.name=foo-service,service.version=v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("type")), ::Istio::Common::DeploymentSuffix);
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("workload")), "foo");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "default");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("cluster")), "");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "v1alpha3");
    checkStructConversion(*r);
  }

  {
    const auto r = ::Istio::Common::convertBaggageToWorkloadMetadata(
        "service.name=foo-service,service.version=v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "v1alpha3");
    checkStructConversion(*r);
  }

  {
    const auto r =
        ::Istio::Common::convertBaggageToWorkloadMetadata("app.name=foo-app,app.version=latest");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-app");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "latest");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-app");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "latest");
    checkStructConversion(*r);
  }

  {
    const auto r = ::Istio::Common::convertBaggageToWorkloadMetadata(
        "service.name=foo-service,service.version=v1alpha3,app.name=foo-app,app.version=latest");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("service")), "foo-service");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("revision")), "v1alpha3");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("app")), "foo-app");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("version")), "latest");
    checkStructConversion(*r);
  }

  {
    const auto r = ::Istio::Common::convertBaggageToWorkloadMetadata("k8s.namespace.name=default");
    EXPECT_EQ(absl::get<absl::string_view>(r->getField("namespace")), "default");
    checkStructConversion(*r);
  }
}

TEST(WorkloadMetadataObjectTest, AppLabelPrecedence) {
  struct TestCase {
    bool raw_present;
    bool qualified_present;
    std::string raw;
    std::string qualified;
    std::string expected;
  };
  const std::vector<TestCase> cases = {
      {true, true, "v1", "v2", "v1"},  {true, false, "svc", "", "svc"},
      {false, true, "", "svc", "svc"}, {true, true, "", "svc", "svc"},
      {true, true, "svc", "", "svc"},  {true, true, "", "", ""},
      {true, false, "", "", ""},       {false, true, "", "", ""},
      {false, false, "", "", ""},
  };
  struct LabelPair {
    absl::string_view raw;
    absl::string_view qualified;
    absl::string_view field;
  };
  // Struct fields are a protobuf Map, so a test cannot control label iteration order. The result
  // is order independent because both values are collected before one is picked.
  for (const auto& keys : {LabelPair{AppNameLabel, AppNameQualifiedLabel, AppNameToken},
                           LabelPair{AppVersionLabel, AppVersionQualifiedLabel, AppVersionToken}}) {
    for (const auto& test_case : cases) {
      SCOPED_TRACE(absl::StrCat(keys.field, ": raw_present=", test_case.raw_present,
                                ", qualified_present=", test_case.qualified_present,
                                ", raw=", test_case.raw, ", qualified=", test_case.qualified));
      Envoy::Protobuf::Struct node;
      auto* labels = (*node.mutable_fields())[LabelsMetadataField].mutable_struct_value();
      if (test_case.raw_present) {
        (*labels->mutable_fields())[keys.raw].set_string_value(test_case.raw);
      }
      if (test_case.qualified_present) {
        (*labels->mutable_fields())[keys.qualified].set_string_value(test_case.qualified);
      }
      auto obj = convertStructToWorkloadMetadata(node);
      EXPECT_EQ(absl::get<absl::string_view>(obj->getField(keys.field)), test_case.expected);
    }
  }
}

TEST(WorkloadMetadataObjectTest, AppLabelsWithCanonicalName) {
  Envoy::Protobuf::Struct node;
  auto* labels = (*node.mutable_fields())[LabelsMetadataField].mutable_struct_value();
  (*labels->mutable_fields())[AppNameLabel].set_string_value("my-service");
  (*labels->mutable_fields())[AppNameQualifiedLabel].set_string_value("microservice");
  (*labels->mutable_fields())["app.kubernetes.io/instance"].set_string_value("my-service");
  (*labels->mutable_fields())[CanonicalNameLabel].set_string_value("microservice");

  auto obj = convertStructToWorkloadMetadata(node);
  EXPECT_EQ(absl::get<absl::string_view>(obj->getField(AppNameToken)), "my-service");
  EXPECT_EQ(absl::get<absl::string_view>(obj->getField(ServiceNameToken)), "microservice");
  auto metadata = convertWorkloadMetadataToStruct(*obj);
  EXPECT_EQ(metadata.fields()
                .at(LabelsMetadataField)
                .struct_value()
                .fields()
                .at(AppNameLabel)
                .string_value(),
            "my-service");
  checkStructConversion(*obj);

  EXPECT_EQ(obj->baggage(), "service.name=microservice,app.name=my-service");
  auto peer = convertBaggageToWorkloadMetadata(obj->baggage());
  EXPECT_EQ(absl::get<absl::string_view>(peer->getField(AppNameToken)), "my-service");
  EXPECT_EQ(absl::get<absl::string_view>(peer->getField(ServiceNameToken)), "microservice");
}

TEST(WorkloadMetadataObjectTest, ConvertFromEmpty) {
  Envoy::Protobuf::Struct node;
  auto obj = ::Istio::Common::convertStructToWorkloadMetadata(node);
  EXPECT_EQ(obj->serializeAsString(), "");
  checkStructConversion(*obj);
}

TEST(WorkloadMetadataObjectTest, ConvertFromEndpointMetadata) {
  EXPECT_EQ(std::nullopt, ::Istio::Common::convertEndpointMetadata(""));
  EXPECT_EQ(std::nullopt, ::Istio::Common::convertEndpointMetadata("a;b"));
  EXPECT_EQ(std::nullopt, ::Istio::Common::convertEndpointMetadata("a;;;b"));
  EXPECT_EQ(std::nullopt, ::Istio::Common::convertEndpointMetadata("a;b;c;d"));
  auto obj = ::Istio::Common::convertEndpointMetadata("foo-pod;default;foo-service;v1;my-cluster");
  ASSERT_TRUE(obj.has_value());
  EXPECT_EQ(obj->serializeAsString(), "workload=foo-pod,cluster=my-cluster,"
                                      "namespace=default,service=foo-service,revision=v1");
}

} // namespace Common
} // namespace Istio
