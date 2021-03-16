#include <map>
#include <vector>

#include "common/buffer/buffer_impl.h"
#include "common/common/hex.h"

#include "extensions/filters/network/mysql_proxy/mysql_utils.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mysql_test_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

/* the api of mysql old password function is */
TEST(AuthHelperTest, OldPassWordHash) {
  /*
   * OldPasswordCases generated from MySQL Server by "set old_passwords=1; SELECT PASSWORD(text);"
   */
  std::map<std::string, std::string> oldPassWordCases = {{
      {"lspbmbnl", "04634f346a43ff51"}, {"sxvpxvng", "0edb7f6258b0580a"},
      {"npzsltxt", "4dd0c97d07484d41"}, {"izosmxhr", "33fe1bd20270bc72"},
      {"mtemsuyl", "17614bd602386385"}, {"kcjypgul", "57257c4b0e224152"},
      {"herxsyge", "201bb5bd30541f27"}, {"ftlpfzvm", "3a4f942d449b9061"},
      {"jywqqfrh", "5960d6ce7f814ccc"}, {"omosqeyy", "4f5a09846cdd86ef"},
      {"ncjjzjhh", "379f21fb7511644f"}, {"zzsmxelk", "7cd63d5e534ca948"},
      {"lwqwfpve", "6e083fea5fe9717f"}, {"jdubrwik", "0fe9dc444a4259f3"},
      {"cogoctit", "1974fdc23962d219"}, {"namdszhy", "76e89bdc5148efe0"},
      {"zlkxtjuh", "2bb60d22103409ad"}, {"flhmfvwt", "7f5cd79c0a58d21f"},
      {"ogwcccqb", "45890ffb79c2f03c"}, {"ynrwbuqm", "4b4e61473903c4d2"},
      {"ipqwmcgj", "3698a07c25dc1a97"}, {"nljxsrqy", "3921fd114429a6e6"},
      {"vzzlcyvm", "386bc3b7782c0379"}, {"vcjpgsfn", "19d31ddb4aa048be"},
      {"ddvtbfhi", "0a426efb0ae239a3"}, {"ehdosgsu", "0512db8036b9d3de"},
      {"exekfjbz", "41e0df8705cab80e"}, {"xialkbfn", "7b5f19d161bbbaef"},
      {"jcpghhfo", "5aca66af047a78b5"}, {"qixefqlq", "574ec4711241808b"},
      {"unlyxufj", "60f9761b33fb784f"}, {"difewfvw", "0500eb5443cdf0b5"},
      {"viemvtrd", "39a422f716fa7d76"}, {"spgulhxe", "4f1bfbee7c7c43fd"},
      {"pkealafr", "410bb4906c6ee405"}, {"uhwesfin", "4b211eeb0102007a"},
      {"fdzgongj", "118415ab577e08e7"}, {"shjgdjul", "6a7ff4c95e7aeba4"},
      {"spisholn", "00c6d81259d142d5"}, {"vojzqdew", "061f0c1c11d00b82"},
      {"jpwivnyw", "114cea307c297a4d"}, {"kzdntqjb", "07ee40ca5724732e"},
      {"yznplsgh", "153e4895794f87f0"}, {"wvzbfusy", "5b9b138e364a3cf3"},
      {"cgaxolxb", "6e2d938a0806323e"}, {"tlpsvudx", "2b10508001af7188"},
      {"ptmscvsq", "35c03bcb493e24ef"}, {"imyjbide", "7f5335df7d9116a2"},
      {"qmwrjnpk", "3dda289c041bc6d7"}, {"lmbnatik", "4b4eec946b3e3050"},
  }};

  for (const auto& old_password_case : oldPassWordCases) {
    auto old_hash = AuthHelper::oldHash(old_password_case.first);
    EXPECT_EQ(old_password_case.second,
              MySQLTestUtils::encodeUint32Hex(old_hash.data(), old_hash.size()));
  }
}

TEST(AuthHelperTest, NativePassWordHash) {
  /*
   * NativePasswordCases generated from MySQL Server by "set old_passwords=0; SELECT
   * PASSWORD(text);" "PASSWORD(text) = SHA1(SHA1(text))"
   */
  std::map<std::string, std::string> nativePassWordCases = {{
      {"lspbmbnl", "448ecbe03d36ae7ae9fa0168e6a01a92abd8f7d6"},
      {"sxvpxvng", "d4228938a4553d9af357c9c807da8bba04a52513"},
      {"npzsltxt", "a1cd14776d4f52c1d3e93febabc3a9357603a0e2"},
      {"izosmxhr", "6e3a92c68473f9d1060ad7d6ae5f78cc5e84c01e"},
      {"mtemsuyl", "59ec607666e2638f6b572aeeb29a09c8912ec7b5"},
      {"kcjypgul", "0f081da5cf58d3d83b968807503de69d87aab0a7"},
      {"herxsyge", "b4784aad9265c0ed90b1e10353c46435ca901269"},
      {"ftlpfzvm", "abdb23d36503f2d434987ab68fe98c1484e89f41"},
      {"jywqqfrh", "599baffb9094455e66c855ab348eb5d319b57e90"},
      {"omosqeyy", "c519aa6344a47a7f8b36c21d70c7da3ac39c93f4"},
      {"ncjjzjhh", "7cd7f5f45db0f8b4164bc44feb3f7c41280a6dd8"},
      {"zzsmxelk", "b10c76dcf2c42c1fed2872509b57011b49e32991"},
      {"lwqwfpve", "221dd48d592f0efd5a78748bca920704cfaecafd"},
      {"jdubrwik", "2c6280d2b7e878dc894ea836ef042175cc84995a"},
      {"cogoctit", "15d8dc7a2720352a6aec67ac5eff3f701cdfcec1"},
      {"namdszhy", "d057f0d66f441be52d1b7f1e4dc624c106592928"},
      {"zlkxtjuh", "f42cf7310a1c7661e5a6a469f602304aa7aba0a3"},
      {"flhmfvwt", "65850a28dc5059ade5d093c14393cafc75c2e51e"},
      {"ogwcccqb", "f2ab16cc3f72de4b1c3c2de76f6d91e4669b6e70"},
      {"ynrwbuqm", "1a95a1dfdd56f8a4fc481236ff3f825e378a5563"},
      {"ipqwmcgj", "6916b39eb7c7cbc9551fc9d444a33605eeb48ea3"},
      {"nljxsrqy", "ae2da3ecd402a1aca1fb9e3845b0e5918064e7bb"},
      {"vzzlcyvm", "1bd1cf9f711e53dc2c42bc8cdae44cc54e0ce5d0"},
      {"vcjpgsfn", "a22d405cfd135ce02e1eb113dcad6b778f05d019"},
      {"ddvtbfhi", "f3f0b21605aed1e5734f8ea3cdd3b6bde2dc81d1"},
      {"ehdosgsu", "fd9d99f4849abba5bd0c46745464af228ae9f547"},
      {"exekfjbz", "9d34353c739925ee98cc9819859a8893e44dcec4"},
      {"xialkbfn", "981bc1edd3beb8ed85cf415408e37687ebb5720d"},
      {"jcpghhfo", "c5ba17d664814e9dcffd63e0492b6728f011d306"},
      {"qixefqlq", "788ad6e588d094f1d80118f4929cdfb7a11fa7e2"},
      {"unlyxufj", "b69424caa75118b457412d31bcf1776862c4867b"},
      {"difewfvw", "8808d829ddba130f13d553b57fe5b938b5fc0b89"},
      {"viemvtrd", "4e09524074d479fa3accc085b148ee780a8319be"},
      {"spgulhxe", "7446286e0148a9304158ec7c0659cc61649e612e"},
      {"pkealafr", "32047f137c2fcae896223dd1b2d8a5b33183b3c7"},
      {"uhwesfin", "d3dac8608d97958ebbd73f56faa8ead7eb9f4765"},
      {"fdzgongj", "5d5b09eef92aee133a920471551200fda80daefa"},
      {"shjgdjul", "eb9d2c8bd7c3f298e5fa2106bf1a355bc274f500"},
      {"spisholn", "ef415c589308274f9cff39fd53c716badf32b726"},
      {"vojzqdew", "45ef7fa3d3e980bdef16246c1f32c99585b3cfc1"},
      {"jpwivnyw", "66a092aadbfc43f1dc0ac4dc3af18c6b4c727726"},
      {"kzdntqjb", "5e09d42667f75c1795507575a5d069afb4b83a19"},
      {"yznplsgh", "3a4732c9e449e3b2e2fceff073b0ef8e6d8ec5fe"},
      {"wvzbfusy", "7f874f2aec995979dd5c05e238e230fa79d051d8"},
      {"cgaxolxb", "df126f29dfe72708e804bbae7101b7afd069bd13"},
      {"tlpsvudx", "563af94b8daf99bd9af725f6441b3dfb4a835ccc"},
      {"ptmscvsq", "d04bd9f7660b52b61b20cca5f6190cac20ca8a38"},
      {"imyjbide", "63523a15d3bd04c05c7d2a6645f59b9fffa5f541"},
      {"qmwrjnpk", "f048ba144e9bc7a6dcf9bee489de9b559124ca43"},
      {"lmbnatik", "73d2c956a3abbe49dd706bbd09b19a44c05016af"},
  }};
  for (const auto& native_password_case : nativePassWordCases) {
    auto actual = AuthHelper::nativeHash(native_password_case.first);
    actual = AuthHelper::nativeHash(actual);
    EXPECT_EQ(Hex::encode(reinterpret_cast<uint8_t*>(actual.data()), actual.size()),
              native_password_case.second);
  }
}

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
