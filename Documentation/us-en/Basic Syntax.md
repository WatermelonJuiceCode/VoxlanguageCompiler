# Vox Basic Syntax (Specified Since v1.3.0)

## Abbreviations
 - "..." : Multiple selections allowed
 - "~" : Optional
 - "T" : Any distinct type
 - "Code" : Code block
 - "Item" : Any iterable data

## Rules/Features
 - The first line must import the standard library: `using Code;`
 - Only postfix increment `i++` exists; there is no prefix increment `++i` like in C++. `i++` first returns the value of `i`, then increments `i` by 1.



## Assignment Expression
### Rules:
 1. Use `;` as the separator, because `,` is the parameter separator in function calls.
 2. It must ultimately return a value.
 3. When assigning at variable definition, it must be wrapped in parentheses.
 4. Do not overuse it, otherwise readability will be "very good."

### Syntax:
 - Variable definition: `T variable = (variable2 = value ; ... ; variable2);`
 - Function call: `Debug.log(variable = value ; ... ; variable);`

### Clever Use:
 - Since Vox only has postfix increment (`i++`), prefix increment can be replaced with an assignment expression: `i++ ; i`

### Example Code:
1. Variable definition:
```vox
using Code;

int a = 10;
Debug.log(a); // Output: 10

int b = (a = 20 ; a);
Debug.log(b); // Output: 20

b = (a += 23 ; a + b);
Debug.log(b); // Output: 63
```
2. Function call:
```vox
using Code;

int a = 10;
Debug.log(a = 40 ; a); // Output: 40

Debug.log(a += 25 ; a); // Output: 65

Debug.log(a -= 13 ; a++); // Output: 52
```



## Restriction Expression / Constraint Expression
### Rules:
 1. This expression is itself the rule 😂

### Syntax:
 - Basic: `T name -> Cls = default;`
 - Combined: `T name -> Cls1 & Cls 2 & ... = default;`
 - Parameterized: `T name -> Cls[arg, ...] = default;`

### Clever Use
 - Can be reused instead of writing one every time. Similar to CSharp's `T name { set; }` set.

### Example Code:
Range control (standard library):
```vox
using Code;
using Code.edit;

int x -> edit.Range[0, 100];
x = 20; // No problem
Debug.log(x); // Output: 20

x = 101; // Error
```
Custom restriction:
```vox
using Code;
using Code.edit;

class Eq(edit.IDE) {
    self _init_(int num, int eqNum) { // int num: the compiler automatically passes the variable's value; int eqNum: the value in square brackets
        if (num != eqNum) { raise ValueException("Error"); }
    }
}

int num -> Eq[20];
num = 20; // OK
num = 15; // ValueException: Error
```



## foreach
### Rules:
 1. Try to keep the number of variables between 1 and 4.

### Syntax:
 - Basic: `foreach (T name, ... ; name in Item, ...) { /* Code */ }`

### Clever Use
 - Iterates simultaneously, driven by the longest data; very useful in data processing.
 - Combined with the standard library's `range` function, it can replace `for`.

### Notes:
 - Driven by the longest; shorter ones are replaced with `null`. You can use `name is null`, `name == null`.


### Example Code:
1. Simple counting:
```vox
using Code;

foreach (int i; i in range(10)) { Debug.log(i); }

/*
Output:
0
1
2
3
4
5
6
7
8
9
*/
```
2. Multi-variable simultaneous iteration (same length):
```vox
using Code;

foreach (int i, j; i in range(10), j in range(1, 11)) {
    Debug.log(j, i, sep="\t");
}

/*
Output:
1       0
2       1
3       2
4       3
5       4
6       5
7       6
8       7
9       8
10      9
*/
```
3. Multi-variable simultaneous iteration (different lengths):
```vox
using Code;

foreach (int i, j; i in range(10), j in range(15)) {
    Debug.log(j, i);
}

/*
Output:
0 0
1 1
2 2
3 3
4 4
5 5
6 6
7 7
8 8
9 9
10 null
11 null
12 null
13 null
14 null
*/
```
4. Using syntactic sugar to detect null:
```vox
using Code;

foreach (int i, j; i in range(10), j in range(15)) {
    Debug.log(j, i is null ? "?" : i); // You can also use i == null
}

/*
Output:
0 0
1 1
2 2
3 3
4 4
5 5
6 6
7 7
8 8
9 9
10 ?
11 ?
12 ?
13 ?
14 ?
*/
```



## Lambda Expression
### Rules:
 1. When passing as an argument, variable names are not required.
 2. When assigning to a variable, variable names are required.

### Syntax:
 - Basic:        `T(~T args, ...) => { /* Code */ }`
 - Capture/Closure: `T(~T args, ...) => ref { /* Code */ }`

### Clever Use
 - If it's just a temporary function, there's no need to write an extra function and consume memory.

### Notes:
 - May trigger UB.

### Example Code
1. Using a variable as a function:
```vox
using Code;

int(int) f = int(int x) => { return x + x; };
Debug.log(f(10)); // Output: 20
```
2. Using in arguments:
```vox
using Code;

void func(int() f) {
    Debug.log(f());
}

func(int() => {
    int count = 0;
    foreach (int i; i in range(10)) { count += i ;}
    return count;
});
// Output: 45
```
3. Capture/Closure:
```vox
using Code;

int count = 1234;

int() f = int() => ref {
    count = 5678;
    return count;
};

Debug.log("before: ", count);   // Output: before:  1234
Debug.log("call: ", f());       // Output: call:  5678
Debug.log("after: ", count);    // Output: after:  5678
```



## "Magic" Methods
### Rules:
 1. Every magic method is named `_xxx_`.
 2. The type should correspond to the magic method; otherwise, a compile-time error will occur.

### Class "Magic" Methods
`self _init_(~T args, ...)`
 -   Purpose: Constructor (overloadable)
 -   Signature: Any type; can also omit types. Convention: return `self`: `self _init_(~T args, ...) { ... }`
 - Trigger point: `new Cls(...)`
 - Example:
```vox
class Point {
    public int x, y;

    public void _init_(int x, int y) {
        self.x = x;
        self.y = y;
    }
}

Point p = new Point(3, 4);
```

`void _del_()`
 -   Purpose: Destructor
 -   Signature: No parameters
 - Trigger point: Object leaves scope / `with` ends
 - Example:
```vox
class Resource {
    public void _init_() {
        Debug.log("[open]");
    }

    public void _del_() {
        Debug.log("[close]");
    }
}

if (true) {
    Resource r = new Resource();   // [open]
}   // Leaves scope → [close]
```

`void _startWith_` & `void _endWith_()`
 -   Purpose: Context enter hook / Context exit hook
 -   Signature: No parameters
 - Trigger point: `with(obj)` begins / `with (obj)` ends
 - Example:
```vox
using Code;
using Code.time;

class Draw {
    private int x, y;
    private bool isStartWith = false;

    self _init_(int x, int y) {
        self.x = x;
        self.y = y;
        self.isStartWith = false;
    }

    void _startWith_() {
        Debug.log("with start and pen up");
        self.isStartWith = true;
        time.sleep(1000);
    }

    void update() {
        if (!self.isStartWith) { time.sleep(1000); }
        foreach (int64 i; i in range(self.x * self.y)) {}
        Debug.suc("OK");
    }

    void _endWith_() {
        Debug.log("with end and pen down");
        self.isStartWith = false;
    }
}

Draw draw = new Draw(1000, 1000);

time.Timer timer = time.Timer();
timer.start();
with (draw) {
    draw.update();
    draw.update();
    draw.update();
}

Debug.log(timer.get());
/*
with start and pen up
OK
OK
OK
with end and pen down
2176
*/
```

`string _string_()`
 -   Purpose: Automatically called when converting to string (or displaying `(string)obj`)
 -   Signature: No parameters
 - Trigger point: `(string)obj`
 - Example:
```vox
class Point {
    public int x, y;
    public void _init_(int x, int y) { self.x = x; self.y = y; }

    public string _string_() {
        return "(" + (string)self.x + ", " + (string)self.y + ")";
    }
}

Point p = new Point(3, 4);
Debug.log("p = ", p);        // p = (3, 4)
Debug.log("" + p);           // (3, 4)
```

`bool _bool_()`
 -   Purpose: Truthiness evaluation
 -   Signature: No parameters
 - Trigger point: `if (obj)` / `&&` / `||` / `!`
  - Example:
```vox
class Box {
    public int count;

    public void _init_(int n) { self.count = n; }

    public bool _bool_() { return self.count > 0; }
}

Box b1 = new Box(5);
Box b2 = new Box(0);

if (b1) { Debug.log("b1 true"); }   // Triggered
if (!b2) { Debug.log("b2 false"); } // Triggered
Debug.log(b1 && b2);                // false
```

`int _int_()`
 -   Purpose: Convert to int
 -   Signature: No parameters
 - Trigger point: `(int)obj`

`char _char_()`
 -   Purpose: Convert to char
 -   Signature: No parameters
 - Trigger point: `(char)obj`

`float _float_()`
 -   Purpose: Convert to float
 -   Signature: No parameters
 - Trigger point: `(float)obj`

`double _double_()`
 -   Purpose: Convert to double
 -   Signature: No parameters
 - Trigger point: `(double)obj`

#### Type Conversion Comprehensive Example Code
```vox
class Temperature {
    public double celsius;
    public void _init_(double c) { self.celsius = c; }
    public double _double_() { return self.celsius; }
    public int _int_() { return (int)self.celsius; }
}

Temperature t = new Temperature(36.6);
double d = (double)t;    // 36.6
int i = (int)t;          // 36
```

`int64 _hash_()`
 -   Purpose: Hash for `Dict` keys
 -   Signature: No parameters
 - Trigger point: Key operations of `Dict<Obj, V>`
 - Example:
```vox
class Id {
    public int value;
    public void _init_(int v) { self.value = v; }

    public int64 _hash_() { return (int64)self.value; }

    public bool _eq_(Id other) { return self.value == other.value; }
}

Dict<Id, string> map = {};
map.add(new Id(1), "one");
Debug.log(map[new Id(1)]);   // one (matched via _hash_ + _eq_)
```

`T _index_(int i)`
 -   Purpose: Index access
 -   Signature: One parameter
 - Trigger point: `obj[index]`
 - Example:
```vox
class Matrix {
    public List<int> data;

    public int _index_(int i) {
        return self.data[i];
    }
}

Matrix m = new Matrix();
m.data = [10, 20, 30];
Debug.log(m[1]);    // 20
```

`T _call_(~T args, ...)`
 -   Purpose: Call object as a function
 -   Signature: Any parameters
 - Trigger point: `obj()`
 - Example:
```vox
class Adder {
    public int base;
    public void _init_(int b) { self.base = b; }

    public int _call_(int x) { return self.base + x; }
}

Adder add10 = new Adder(10);
Debug.log(add10(5));    // 15
```

`T _neg_()`
 -   Purpose: Unary minus
 -   Signature: No parameters, returns its own type
 - Trigger point: `-obj`

`T _not_()`
 -   Purpose: Unary logical NOT
 -   Signature: No parameters, returns any type
 - Trigger point: `!obj`

#### Unary Operator Comprehensive Example Code
```vox
class Vec2 {
    public double x, y;
    public void _init_(double x, double y) { self.x = x; self.y = y; }

    public Vec2 _neg_() { return new Vec2(-self.x, -self.y); }
}

Vec2 v = new Vec2(3, 4);
Vec2 w = -v;       // (-3, -4)

class Truthy {
    public bool value = false;
    public bool _not_() { return !self.value; }
}
```

`bool _eq_()` / `bool _ne_()`
 -   Purpose: Equality / Inequality
 -   Signature: 1 parameter (same type), returns `bool`
 - Trigger point: `obj1 == obj2` / `obj1 != obj2`
 - Example:
```vox
class Point {
    public int x, y;
    public void _init_(int x, int y) { self.x = x; self.y = y; }

    public bool _eq_(Point other) {
        return self.x == other.x && self.y == other.y;
    }
}

Point a = new Point(1, 2);
Point b = new Point(1, 2);
Debug.log(a == b);   // true
Debug.log(a != b);   // false (auto-generated != as !(_eq_))
```

`bool _lt_()` / `bool _le_()` / `bool _gt_()` / `bool _ge_()`
 -   Purpose: Size comparison
 -   Signature: 1 parameter (same type), returns `bool`
 - Trigger point: `<` / `<=` / `>` / `>=`
 - Example:
```vox
class Version {
    public int major, minor;
    public void _init_(int M, int m) { self.major = M; self.minor = m; }

    public bool _lt_(Version o) {
        if (self.major != o.major) { return self.major < o.major; }
        return self.minor < o.minor;
    }
}

Version v1 = new Version(1, 2);
Version v2 = new Version(2, 0);
Debug.log(v1 < v2);    // true
Debug.log(v1 > v2);    // false (auto-generated > as _b < _a)
Debug.log(v1 <= v2);   // true (auto-generated <= as !(_b < _a))
```

`T _add_()` / `T _sub_()` / `T _mul_()` / `T _div_()` / `T _mod_()`
 -   Purpose: Binary arithmetic
 -   Signature: 1 parameter (same type), returns `bool`, returns its own type
 - Trigger point: `+` / `-` / `*` / `/` / `%`
 - Example:
```vox
class Vec2 {
    public double x, y;
    public void _init_(double x, double y) { self.x = x; self.y = y; }

    public Vec2 _add_(Vec2 o) { return new Vec2(self.x + o.x, self.y + o.y); }
    public Vec2 _sub_(Vec2 o) { return new Vec2(self.x - o.x, self.y - o.y); }
    public Vec2 _mul_(Vec2 o) { return new Vec2(self.x * o.x, self.y * o.y); }
}

Vec2 a = new Vec2(1, 2);
Vec2 b = new Vec2(3, 4);
Vec2 c = a + b;    // (4, 6)
Vec2 d = a - b;    // (-2, -2)
Vec2 e = a * b;    // (3, 8)
```

### In One Sentence

**16 magic methods**, divided into 5 categories:

| Category | Methods |
|---|---|
| **Lifecycle** | `_init_` / `_del_` / `_startWith_` / `_endWith_` |
| **String / Truthiness** | `_string_` / `_bool_` |
| **Type Conversion** | `_int_` / `_char_` / `_float_` / `_double_` |
| **Container / Call** | `_hash_` / `_index_` / `_call_` |
| **Operators** | `_neg_` / `_not_` / `_eq_` `_ne_` / `_lt_` `_le_` `_gt_` `_ge_` / `_add_` `_sub_` `_mul_` `_div_` `_mod_` |

**Key Points**:
- Write only `_eq_` → `!=` is auto-generated
- Write only `_lt_` → `>`, `<=`, `>=` are auto-generated
- `_string_` / `_bool_` / `_int_` etc. take **no parameters**
- `_add_` etc. take **1 parameter, same type, return same type**
- `_del_` / `_with_` take **no parameters, return void**
- `_hash_` takes **no parameters, returns int64**



## Parameter Features / Function Features
### Syntax:
 - Basic: `T name(~T args, ...)`
 - Positional-only parameters: `T name(~T args, ..., /)`
 - Keyword-only parameters: `T name(*, ~T args, ...)`
 - Combined: `T name(~T args, ..., /, *, ~T args2, ...)`
 - Read-only: `T name(~ref const T args, ...)` / `T name(~const ref T args, ...)`
 - Method does not modify self (in class): `T name(...) const { /* Code */ }`