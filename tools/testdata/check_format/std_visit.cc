namespace Envoy {
  struct SomeVisitorFunctor {
      template<typename T>
      void operator()(const T& i) const {}
  };

  void foo() {
    absl::variant<int, float> foo(1.23);
    SomeVisitorFunctor visitor;
    std::visit(visitor, foo);
  }
} // namespace Envoy
