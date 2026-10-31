# 追加类（CanAppend & Append）

`CanAppend` 和 `Append`，我们可以把它理解为：**Vox 原生支持的“插件式类扩展”机制**。

如果你来自 C#，它类似 `Extension Methods`；如果你来自 Rust，它类似 `impl` 块。但 Vox 的这套机制自带强类型检查和权限控制。

---

### 1. 这是什么？为什么要用它？

在传统 C++ 或 Java 中，如果你引入了一个第三方库的 `Entity` 类，但发现它少了一个 `heal()` 方法，你通常只能：
1. 继承它（但可能会破坏原有对象的类型依赖）。
2. 写一个 `EntityHeal(entity)` 的工具函数（调用起来不自然）。

在 Vox 中，只要原库作者允许，你可以**直接给这个类“追加”方法**，调用时就像它原生自带的一样。

---

### 2. 基础语法：两步走

**第一步：原库声明允许扩展**
```vox
// 库作者 A 写的代码
CanAppend class Player {
    public string name;
    private int hp;
}
```

**第二步：你进行扩展追加**
```vox
// 游戏开发者 B 写的代码
Append class Player {
    void heal(int amount) {
        other.hp += amount; // 访问原对象
    }
}
```

**调用时极度自然：**
```vox
Player p = new Player();
p.heal(10); // 就像调用原类的方法一样
```

---

### 3. 核心规则（必知必会）

*   **无中生有？不行**：如果原类没有 `CanAppend` 声明，你直接写 `Append` 会报错。
*   **改变结构？不行**：`Append` 块里**只能写方法**。不能加字段、不能写构造函数、不能写析构函数、不能声明信号。
*   **强覆盖？不行**：原类如果有 `void test()`，你不能在 `Append` 里重写它（`override` 会报错），也不能写同名同参数的方法。这是“追加”，不是“拦截”。
*   **可以多次追加**：你可以分多个 `Append class Player` 块写，只要方法签名不冲突。

---

### 4. 权限控制：如何访问 private 成员？

默认情况下，`Append` 就像外部类一样，**只能访问 public 成员**。如果你强行访问 private，Vox 编译器会直接把你毙掉。

如果原库作者信任你的扩展，需要在 `CanAppend` 里开绿灯：

```vox
CanAppend class Player {
    private int hp;

    // 方式一：通配符授权（所有 Append 方法都能访问 private）
    friend *; 

    // 方式二：精准授权（只允许涉及这些名字的方法访问 private）
    // friend heal, takeDamage; 
}
```
有了 `friend *;`，你扩展的 `heal()` 就能直接 `other.hp += 10;` 了。

---

### 5. 上下文关键字：`self` 与 `other`

在 `Append` 的方法里，有两个非常特殊的上下文关键字：

*   **`other`**：代表**宿主对象**（也就是 `Player` 实例本身）。如果你想读写原类的属性、调用原类的方法，一律用 `other`。
*   **`self`**：代表**当前的 Append 上下文**。它专门用来调用**其他 Append 方法**。

**代码示例：**
```vox
Append class Player {
    void heal(int amount) {
        other.hp += amount; 
        self.logHeal(amount); // 调用另一个 Append 方法
    }

    void logHeal(int amount) {
        Debug.log("玩家恢复了 " + amount + " 点血量");
    }
}
```
*注意：在 `Append` 里不能用 `self.hp`。你要访问原类的成员，只能用 `other.hp`。*

---

### 6. 继承与多态：`super` 怎么用？

如果你的 `Player` 继承自 `BaseClass`，你在 `Append` 里依然可以通过 `super` 调用基类方法。

```vox
Append class Player {
    void attack() {
        super.playSound("slash"); // 调基类的方法
        Debug.log("Player attacks!"); 
    }
}
```

---

### 7. 什么时候适合用 `Append`？（最佳实践）

*   **插件架构**：你的游戏引擎核心代码只包含最基础的功能。DLC 或 Mod 作者可以通过 `Append` 给 `Enemy` 类加上 `dropLoot()` 方法。
*   **职责分离**：原类只保留核心状态，把 `save()` / `load()` 等序列化操作，交给 `Append` 去做。
*   **测试**：测试代码可以给类追加 `assertIsValid()` 方法，而不污染生产环境。

### 什么时候**不该**用？
*   需要给类加私有字段（比如加个 `isDead` 标志）—— `Append` 绝对做不到，你应该用 `Dict<Player, bool>` 或者修改原类。
*   需要改变原有方法的行为（重写）—— 你应该通过继承（`class MyPlayer : Player`）来解决。

---

### 结语
`CanAppend` + `Append` + `friend` 的组合，赋予了 Vox 极强的模块化能力，同时在编译期保持了绝对的类型安全。用好它，你的软件架构会变得极其解耦！