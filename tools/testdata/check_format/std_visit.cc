namespace Envoy {
  struct SomeVisitorFunctor {
      template<typename T>
      void operator()(const T& i) const {}
  };

  void foo() {
    absl::variant<int, std::string> foo = std::string("foo");
    SomeVisitorFunctor visitor;
    std::visit(visitor, foo);
  }
} // namespace Envoy
