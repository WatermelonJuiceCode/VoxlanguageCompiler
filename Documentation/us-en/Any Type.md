# The `any` Type

## What is `any`?
`any` is an important type in Vox that can be used to represent anything.
It is not a "dynamic type," but rather a type-erased **dynamic object**.

## How to use `any`?
```vox
using Code;

class Entity {}

Entity player = new Entity(); // Instantiate Entity and store it in player
List<any> anything = [10, 20, true, false, "Hello", " ", "World", player]; // Define a container (List) that can hold anything
Debug.log(anything);
```
Output: `[10, 20, true, false, Hello,  , World, <Entity object at 0000020197ee1d70>]`
Without the `any` type, you would have to (in C++):
1. Write a base class `Object`.
2. Make `int`, `bool`, and `string` inherit from `Object` and override `toString()`.
3. Define a custom `Entity` that inherits from `Object` and overrides `toString()`.
4. Use `std::vector<std::shared_ptr<Object>>` to hold them.
5. Iterate and print.

Without dozens or hundreds of lines of code, it's simply impossible to write.
In Vox, however, all you need is a single `List<any>` plus the `_string_()` magic method. The syntax is minimalist, but the underlying logic is rock-solid!
`any`      —`anything`
(Note: If you don't like printing addresses, add a `_string_` magic method to `Entity` that returns a string, e.g., `return "Entity()"`. See [Magic Methods](Basic%20Syntax.md#Class-"Magic"-Methods) for details.)

## How is `any` implemented?
`any` is implemented entirely in C++17.
Its implementation is extremely hardcore, incorporating reference counting, vtable stubbing, stack memory copying, function closure dispatch, and arithmetic operator overloading. It is currently the most complex class in the Vox runtime.
In Vox, the `any` type maps to `vox::Any`. This is a **type-erased** dynamic object, similar to C++'s `std::any`, C#'s `object`, or Python's dynamic typing.

Its implementation is extremely hardcore, incorporating **reference counting, vtable stubbing, stack memory copying, function closure dispatch, and arithmetic operator overloading**. It is currently the most complex class in the Vox runtime.

Let's peel back this "magic" layer by layer:

---

### 1. Core Structure: What does it actually store?
Look at the members of `class Any` in `vox_runtime.h`:

```cpp
class Any {
public:
    std::shared_ptr<void> _ptr;              // 1. Generic pointer (type erasure)
    const std::type_info* _type = nullptr;   // 2. Type info (for is<T> and typeid comparison)

    AnyNumKind _numKind = AnyNumKind::None;  // 3. Numeric cache (None / Int / Float)
    double     _numValue = 0;                // 4. Actual numeric value (for fast arithmetic)

    String (*_to_str)(const void*) = nullptr;     // 5. To-string function pointer
    String (*_type_name)(const void*) = nullptr;  // 6. Get-type-name function pointer
    bool   (*_to_bool)(const void*) = nullptr;    // 7. To-bool function pointer
    bool   (*_equals)(const void*, const void*) = nullptr; // 8. Equality comparison function pointer

    std::function<Any(const std::vector<Any>&)> _caller; // 9. Function call wrapper
};
```

**Interpretation:**
- `_ptr`: Uses `std::shared_ptr<void>` to erase the concrete type and provide automatic memory management.
- Function pointers (`_to_str` / `_to_bool`, etc.): These are lambdas dynamically generated in `Any`'s **constructor** based on the passed-in `T`. Since they capture the concrete type `T`, they cast back via `static_cast<T*>(void_ptr)` internally to perform the specific operation.
- This is the classic **Virtual Table simulation**, but because it doesn't rely on polymorphic inheritance, C++ can easily stuff `int`, `std::string`, or even `Lambda` into it.

---

### 2. Universal Construction: How do you put anything in?

```cpp
template<typename T, typename = ...>
Any(T&& v)
    : _ptr(std::make_shared<std::decay_t<T>>(std::forward<T>(v))), // Heap allocation and copy
      _type(&typeid(std::decay_t<T>))
{
    using U = std::decay_t<T>;

    // ① Numeric cache: if it's int/double, extract it directly to speed up later arithmetic
    if constexpr (std::is_arithmetic_v<U>) { ... }

    // ② Mount function pointers: capture the concrete type U via lambda
    _to_str = [](const void* p) -> String {
        return vox_str(*static_cast<const U*>(p)); // Restore U, call the specific to-string
    };
    _to_bool = [](const void* p) -> bool {
        return vox_truthy(*static_cast<const U*>(p)); // Restore U, call the specific truthiness check
    };
    
    // ... etc. ...
}
```

The **cost** of this design is: even if you store a single `int`, it will `make_shared<int>` once on the heap.

---

### 3. Unboxing & Type Checking: How do you get it back?

```cpp
    // Exact type check
    template<typename T>
    bool is() const {
        return _type && *_type == typeid(std::decay_t<T>);
    }

    // Turn void* back into T*
    template<typename T>
    T* try_cast() const {
        using U = std::decay_t<T>;
        if (!_ptr || !_type || *_type != typeid(U)) return nullptr;
        return static_cast<U*>(_ptr.get());
    }
```
When calling `vox_cast_int(const Any&)`:
```cpp
    if (auto p = a.try_cast<int>())        return *p;
    if (auto p = a.try_cast<long long>())  return (int)*p;
    if (auto p = a.try_cast<double>())     return (int)*p;
    return 0;
```
This is the "graceful degradation" unboxing logic: it might not be an `int`, but as long as you stored a numeric type, it can be safely converted to `int`.

---

### 4. Function Calls: How do you call a Lambda stored inside Any?

```cpp
    template<typename... Args>
    Any _call_(Args&&... args) const {
        if (!_caller) return Any{};
        std::vector<Any> vec = { Any(std::forward<Args>(args))... }; // Convert arguments back to Any
        return _caller(vec); // Call the stored lambda
    }
```
If the C++ `Any` stores a `std::function<int(int)>`, the constructor wraps it like this:

```cpp
        if constexpr (IsStdFunction<U>::value) {
            _caller = makeCallerFromStdFn<U>(_ptr.get());
        }
```
`makeCallerFromStdFn` internally uses **template metaprogramming** (`std::index_sequence`) to expand `std::vector<Any>`, restoring each argument to its concrete type via `castAnyTo<T>(args)`, then calls the original function. This is why in Vox, `any` can be called directly like a function.

---

### 5. Arithmetic Operations: How does `any + 1` not throw an error?

Through the previously stored `_numKind` and `_numValue`.

```cpp
    Any arith(const Any& o, char op) const {
        // String concatenation takes priority
        if (op == '+' && (is<String>() || o.is<String>())) {
            return Any(toString() + o.toString());
        }
        if (_numKind == AnyNumKind::None || o._numKind == AnyNumKind::None) return Any{};

        double a = _numValue;
        double b = o._numValue;
        bool useFloat = (_numKind == AnyNumKind::Float || o._numKind == AnyNumKind::Float);

        switch (op) {
            case '+': if (useFloat) return Any(a + b); else return Any((long long)a + (long long)b);
            // ...
        }
    }
```
So when the Vox compiler sees that a `BinaryNode`'s `useAnyOps` flag is set, it directly generates `vox_any_add(a, b)`.

---

### 6. What's good about this design?

1. **Extremely flexible**: Seamlessly supports `int`, `double`, `String`, `List<T>`, `Dict<K,V>`, user-defined `Class`, and even `Lambda`.
2. **Memory safe**: `_ptr` is a `shared_ptr`, so even if you put a local variable into `any` and return it, there will be no dangling pointer (because it's copied onto the heap).
3. **Dynamic dispatch**: Similar to Python's duck typing. For example, `a.size()`, `Any` implements:

```cpp
    int size() const {
        if (auto p = try_cast<String>())              return p->size();
        if (auto p = try_cast<List<Any>>())           return p->size();
        if (auto p = try_cast<List<String>>())        return p->size();
        if (auto p = try_cast<Dict<Any, Any>>())      return p->size();
        return 0;
    }
```

### 7. The Cost (Why Vox still needs `T?` (nullable types))

`Any` is extremely "heavy":
- Every `int` stuffed into it requires a heap allocation.
- Even a simple addition goes through `_numKind` checks and function pointer calls.
- If `Any` is overused, the C++ compiler will also become extremely slow to compile due to excessive template instantiation (this is one of the culprits behind OOM).

Therefore, in Vox, **avoid using `any` whenever possible**. This is why we just added `int?` — to replace the high cost of `any` with the zero-cost abstraction of `std::optional<int>`.

The `Any` you're looking at now is a "universal bridge" in the runtime, supporting `argparse`'s dynamic parsing, `urllib`'s dynamic parameters, and Vox's extremely handy `Debug.log` variadic printing. It's not just compiler technology — it's practically a C++ work of art.