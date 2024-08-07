int getSomeVariable() {
  static int some_variable = 0;
  some_variable++;
  return some_variable;
}
