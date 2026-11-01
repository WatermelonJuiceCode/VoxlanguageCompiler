# 关键字`in`

在 Vox 语言中，`in` 是一个非常高频且优雅的关键字。它主要有两个核心作用：**一是作为 `foreach` 循环的“指南针”，用于遍历数据；二是作为“包含关系”运算符，用于判断元素是否在容器中。**

下面我为你全面介绍 `in` 语法的各种用法。

---

### 一、 遍历的艺术：`foreach` 中的 `in`

Vox 的 `foreach` 循环通过 `in` 关键字将迭代变量与可迭代对象连接起来。

#### 1. 基础遍历（单变量）
这是最常见的用法，遍历列表、字典、字符串等：

```vox
List<string> fruits = ["apple", "banana", "orange"];

// 遍历列表（元素）
foreach (string fruit; fruit in fruits) {
    Debug.log("我喜欢吃: " + fruit);
}

// 遍历字典（默认遍历 Key）
Dict<string, int> ages = {"Alice": 20, "Bob": 25};
foreach (string name; name in ages) {
    Debug.log(name + " 的年龄是 " + string(ages[name]));
}

// 遍历字符串（按 UTF-8 字符级）
string text = "你好Vox";
foreach (string ch; ch in text) {
    Debug.log(ch); // 依次输出 你、好、V、o、x
}
```

#### 2. 多变量并行遍历（进阶）
Vox 支持在同一个 `foreach` 中遍历多个集合（**按最长的集合决定循环次数**）：

```vox
// 同时遍历两个列表
foreach (int i, int j; i in range(10), j in range(15)) {
    Debug.log(i, j);
}
```
> **⚠️ 注意**：当集合长度不一致时，较短的集合将“耗尽”。如果是非可空类型（如 `int`），会用默认值（`0`）代替，且编译器会给出 warning。为了安全，建议使用可空类型 `?`：
> ```vox
> // 前 10 次正常打印，后 5 次 i 会变成 null
> foreach (int? i, int j; i in range(10), j in range(15)) {
>     Debug.log(i, j); 
> }
> ```

#### 3. 让自定义类支持 `in` 遍历
如果你希望自己的类也能被 `foreach` 遍历，只需要在类中实现迭代协议：`_begin_()`、`_done_()`、`_next_()`、`_current_()`。

---

### 二、 判断的艺术：作为成员运算符的 `in`

类似于 Python，Vox 的 `in` 可以作为一个布尔运算符，检查某个值是否存在于目标容器中。底层会转换为 `vox_in(left, right)`。

#### 1. 字符串查找（子串）
```vox
string sentence = "Vox is a modern language";
if ("modern" in sentence) {
    Debug.log("找到了 'modern'");
}
```

#### 2. 列表 / 集合查找（元素）
```vox
List<int> nums = [10, 20, 30, 40];
if (20 in nums) {
    Debug.log("列表包含 20");
}
```

#### 3. 字典查找（Key）
```vox
Dict<string, int> scores = {"Tom": 95, "Jerry": 88};
if ("Tom" in scores) {
    Debug.log("Tom 在字典中");
}
```
> **提示**：字典的 `in` 只检查 **Key**，不检查 Value。

#### 4. 泛型 / 自定义类型查找
底层的 `vox_in` 提供了一个 SFINAE 兜底：只要你的容器（包括 `Any` 类型）拥有一个 `contains` 方法，`in` 语法就能无缝支持它。

---

### 三、 避坑指南

1. **区分 `in` 和 `==`**：`in` 判断的是“容器中是否包含”，而 `==` 是判断“是否相等”。
2. **不要混淆 `foreach` 的 `in` 和逻辑运算符 `in`**：
   * `foreach` 里的 `in` 是语法结构（声明循环体）。
   * `if ("a" in list)` 里的 `in` 是布尔表达式。
3. **性能提醒**：如果在大列表上频繁使用 `in`（`item in List`），由于底层是 `O(n)` 的遍历，如果性能要求极高，建议改用 `Dict`（底层如果是 `map` 则是 `O(log n)`，如果是 `unordered_map` 则是 `O(1)`）。

`in` 语法让 Vox 的代码读起来就像自然的英语（例如：`if user in blacklist`），既优雅又强大！