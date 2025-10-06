def cmp_pre(a; b):
  .
  | if (a == [] and b == []) then 0
    elif (a == []) then 1    # empty pre-release > any pre-release
    elif (b == []) then -1
    else
      reduce range(0; max([a|length, b|length])) as $i (0;
        if . != 0 then . else
          (a[$i]? // 0) as $ai
          | (b[$i]? // 0) as $bi
          | if ($ai | test("^[0-9]+$")) and ($bi | test("^[0-9]+$")) then
              ($ai | tonumber) - ($bi | tonumber)
            else
              if $ai == $bi then 0 elif $ai < $bi then -1 else 1 end
            end
        end
      )
    end;

# Compare two versions
# returns -1 if v1 < v2, 0 if equal, 1 if v1 > v2
def cmp(v1; v2):
  .
  | (v1 as $a | v2 as $b
     | if ($a.main? and $b.main?) then
         # numeric semver comparison
         reduce range(0;3) as $i (0;
           if . != 0 then . else $a.main[$i] - $b.main[$i] end
         )
       else
         # fallback: opaque comparison → string equality or lexicographic
         if $a.string == $b.string then 0
         elif $a.string < $b.string then -1
         else 1
         end
       end
    );

def eq(v1; v2): cmp(v1; v2) == 0;
def lt(v1; v2): cmp(v1; v2) < 0;
def lte(v1; v2): cmp(v1; v2) <= 0;
def gt(v1; v2): cmp(v1; v2) > 0;
def gte(v1; v2): cmp(v1; v2) >= 0;

def parse(v):
  .
  | (v // "*") as $v
  | if $v == "*" then null
    else
      ($v | capture("^(?<main>[0-9]+(?:\\.[0-9]+){0,2})(?:-(?<pre>[^+]+))?(?:\\+(?<build>.+))?$")? // null) as $m
      | if $m == null then
          {string: $v}
        else
          ($m.main + (if $m.pre? == null then "" else "-" + $m.pre end) + (if $m.build? == null then "" else "+" + $m.build end)) as $reconstructed
          | if $reconstructed != $v then
              # leftover garbage → treat as opaque
              {string: $v}
            else
              $m
              | .main |= (split(".") | map(tonumber) + [0,0,0])[0:3]
              | .pre |= (if .pre == null then [] else split(".") end)
              | .build |= (if .build == null then [] else split(".") end)
              | .string = $v
            end
        end
  end;
