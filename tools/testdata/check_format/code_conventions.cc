namespace Envoy {

void foo() {
  EXPECT_CALL(a, b).Times(1);
  EXPECT_CALL(a, b)
    .Times(1);
  EXPECT_CALL(a, b).Times(1).WillRepeatedly(foo);
  EXPECT_CALL(a, b).Times(1).WillOnce(foo);
  Stats::ScopePtr scope;
}

}
