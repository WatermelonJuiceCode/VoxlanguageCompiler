# `any`类型

## `any`是什么？
`any`是Vox中一个重要的类型，它可以用来表示任何东西。  
它不是一个“动态类型”，而是一个类型擦除的**动态对象**

## 如何使用`any`？
```vox
using Code;

class Entity {}

Entity player = new Entity(); // 实例化Entity并存到player中
List<any> anything = [10, 20, true, false, "Hello", " ", "World", player]; // 定义一个可装下万物的容器（List）
Debug.log(anything);
```
输出`[10, 20, true, false, Hello,  , World, <Entity object at 0000020197ee1d70>]`  
在没有`any`类型时，你得（C++）：
1. 写一个基类 Object。  
2. 让 int、bool、string 都继承 Object 并重写 toString()。  
3. 自定义 Entity 继承 Object，重写 toString()。  
4. 用 `std::vector<std::shared_ptr<Object>>` 装它们。  
5. 遍历并打印。

没个几十上百行代码，根本写不出来。  
而在 Vox 里，只需要一句 List<any> 加上 _string_() 魔法方法。语法层面上是极简的，底层逻辑是极硬的！  
`any`&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;——`anything`  
（注：如果你不喜欢打印地址的话，在Entity里加`_string_`魔法方法并返回一个string比如`return "Entity()"`，详情请看[魔法方法](基础语法.md#魔法方法)）

## `any`是如何实现的？
`any`完全由C++17实现
它实现得极其硬核，包含了 引用计数、虚表打桩、栈内存拷贝、函数闭包分发、算术运算重载，是目前 Vox 运行时最复杂的类。
在 Vox 里，`any` 类型映射到了 `vox::Any`。这是一个 **类型擦除（Type Erasure）** 的动态对象，类似于 C++ 的 `std::any`、C# 的 `object` 或 Python 的动态类型。

它实现得极其硬核，包含了 **引用计数、虚表打桩、栈内存拷贝、函数闭包分发、算术运算重载**，是目前 Vox 运行时最复杂的类。

我们把这套“魔法”一层层剥开：

---

### 1. 核心结构：它到底存了什么？
看 `vox_runtime.h` 里 `class Any` 的成员：

```cpp
class Any {
public:
    std::shared_ptr<void> _ptr;              // 1. 泛型指针（类型擦除）
    const std::type_info* _type = nullptr;   // 2. 类型信息（用于 is<T> 和 typeid 比较）

    AnyNumKind _numKind = AnyNumKind::None;  // 3. 数值缓存（None / Int / Float）
    double     _numValue = 0;                // 4. 实际数值（方便快速算术）

    String (*_to_str)(const void*) = nullptr;     // 5. 转字符串函数指针
    String (*_type_name)(const void*) = nullptr;  // 6. 获取类型名函数指针
    bool   (*_to_bool)(const void*) = nullptr;    // 7. 转 bool 函数指针
    bool   (*_equals)(const void*, const void*) = nullptr; // 8. 相等比较函数指针

    std::function<Any(const std::vector<Any>&)> _caller; // 9. 函数调用封装
};
```

**解读：**
- `_ptr`：用 `std::shared_ptr<void>` 抹除了具体类型，并且自带自动内存管理。
- 函数指针（`_to_str` / `_to_bool` 等）：它们是在 `Any` 的**构造函数**里，根据传入的 `T` 动态生成的 lambda。由于它们捕获了具体的类型 `T`，在内部通过 `static_cast<T*>(void_ptr)` 还原回去，从而执行具体操作。
- 这就是经典的 **Virtual Table 模拟**，但由于不依赖多态继承，C++ 可以轻松把 `int`、`std::string`、甚至是 `Lambda` 塞进去。

---

### 2. 万能构造：怎么塞进任何东西？

```cpp
template<typename T, typename = ...>
Any(T&& v)
    : _ptr(std::make_shared<std::decay_t<T>>(std::forward<T>(v))), // 堆分配并拷贝
      _type(&typeid(std::decay_t<T>))
{
    using U = std::decay_t<T>;

    // ① 数值缓存：如果是 int/double，直接提取出来，加速后面的算术运算
    if constexpr (std::is_arithmetic_v<U>) { ... }

    // ② 挂载函数指针：通过 Lambda 捕获具体类型 U
    _to_str = [](const void* p) -> String {
        return vox_str(*static_cast<const U*>(p)); // 还原 U，调用具体的转字符串
    };
    _to_bool = [](const void* p) -> bool {
        return vox_truthy(*static_cast<const U*>(p)); // 还原 U，调用具体真值判断
    };
    
    // ... 等等 ...
}
```

这套设计的**代价**是：哪怕你存一个 `int`，它也会在堆上 `make_shared<int>` 一次。

---

### 3. 拆箱与类型检测：如何变回来？

```cpp
    // 精确检查类型
    template<typename T>
    bool is() const {
        return _type && *_type == typeid(std::decay_t<T>);
    }

    // 把 void* 变回 T*
    template<typename T>
    T* try_cast() const {
        using U = std::decay_t<T>;
        if (!_ptr || !_type || *_type != typeid(U)) return nullptr;
        return static_cast<U*>(_ptr.get());
    }
```
当调用 `vox_cast_int(const Any&)` 时：
```cpp
    if (auto p = a.try_cast<int>())        return *p;
    if (auto p = a.try_cast<long long>())  return (int)*p;
    if (auto p = a.try_cast<double>())     return (int)*p;
    return 0;
```
这就是“优雅降级”的拆箱逻辑：你不一定是 `int`，但只要你存的是数值类型，我就能安全地转成 `int`。

---

### 4. 函数调用：Any 里面装了 Lambda 怎么调？

```cpp
    template<typename... Args>
    Any _call_(Args&&... args) const {
        if (!_caller) return Any{};
        std::vector<Any> vec = { Any(std::forward<Args>(args))... }; // 把参数重新转成 Any
        return _caller(vec); // 调用内部存好的 lambda
    }
```
如果 C++ 里的 `Any` 装了一个 `std::function<int(int)>`，构造函数里会这样包装：

```cpp
        if constexpr (IsStdFunction<U>::value) {
            _caller = makeCallerFromStdFn<U>(_ptr.get());
        }
```
`makeCallerFromStdFn` 内部用**模板元编程**（`std::index_sequence`）把 `std::vector<Any>` 展开，逐个用 `castAnyTo<T>(args)` 还原成具体参数类型，然后调用原函数。这就是为什么在 Vox 里，`any` 可以像函数一样直接调用。

---

### 5. 算术运算：怎么做到 `any + 1` 不报错？

通过前面存的 `_numKind` 和 `_numValue`。

```cpp
    Any arith(const Any& o, char op) const {
        // 字符串拼接优先
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
所以 Vox 编译器看到 `BinaryNode` 的 `useAnyOps` 被标记时，就会直接生成 `vox_any_add(a, b)`。

---

### 6. 这个设计好在哪里？

1. **极度灵活**：无缝支持 `int`, `double`, `String`, `List<T>`, `Dict<K,V>`, 用户自定义 `Class`, 甚至 `Lambda`。
2. **内存安全**：`_ptr` 是 `shared_ptr`，哪怕你把局部变量塞进 `any` 返回出去，也不会有野指针（因为它拷贝到堆上了）。
3. **动态派发**：类似于 Python 的鸭子类型。比如 `a.size()`，`Any` 里写好了：

```cpp
    int size() const {
        if (auto p = try_cast<String>())              return p->size();
        if (auto p = try_cast<List<Any>>())           return p->size();
        if (auto p = try_cast<List<String>>())        return p->size();
        if (auto p = try_cast<Dict<Any, Any>>())      return p->size();
        return 0;
    }
```

### 7. 代价（为什么 Vox 还需要 `T?`(可空类型)）

`Any` 是极其“重”的：
- 任何 `int` 塞进去都要一次堆分配。
- 哪怕只是一个加法，也要经过 `_numKind` 判断、函数指针调用。
- 如果滥用 `Any`，C++ 编译器也会因为模板实例化过多导致编译极其缓慢（这就是造成 OOM 的元凶之一）。

因此，在 Vox 里，**能不用 `any` 就不用 `any`**。这就是为什么我们刚加了 `int?`——把 `any` 的高昂成本，换成 `std::optional<int>` 的零成本抽象。

你现在看到的这套 `Any`，是运行时的一座“万能桥”，支撑起了 `argparse` 的动态解析、`urllib` 的动态参数、以及 Vox 极其好用的 `Debug.log` 变参打印。它不仅是编译器技术，简直是一件 C++ 艺术品。