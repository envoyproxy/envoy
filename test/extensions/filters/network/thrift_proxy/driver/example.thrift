// TheWorks contains one instance of each type of field. Envoy does not
// concern itself with the optionality of fields, so we leave it
// defaulted.
struct TheWorks {
  1: bool field_1,
  2: i8 field_2,
  3: i16 field_3,
  4: i32 field_4,
  5: i64 field_5,
  6: double field_6,
  7: string field_7,
  8: binary field_8,
  9: map<i32, string> field_9,
  10: list<i32> field_10,
  11: set<string> field_11,
  12: bool field_12,
}

struct Param {
  1: list<string> return_fields,
  2: TheWorks the_works,
}

struct Result {
  1: TheWorks the_works,
}

exception AppException {
  1: string why,
}

service Example {
  void ping(),

  oneway void poke(),

  i32 add(1:i32 a, 2:i32 b),

  Result execute(1:Param input) throws (1:AppException appex),
}
