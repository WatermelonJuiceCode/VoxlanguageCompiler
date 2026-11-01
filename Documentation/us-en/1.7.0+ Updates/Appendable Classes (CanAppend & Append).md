# Appendable Classes (CanAppend & Append)

`CanAppend` and `Append` can be understood as: **Vox's natively supported "plugin‑style class extension" mechanism**.

If you come from C#, it's similar to `Extension Methods`; if you come from Rust, it's similar to `impl` blocks. But Vox's mechanism comes with built‑in strong type checking and permission control.

---

### 1. What is it? Why use it?

In traditional C++ or Java, if you import a third‑party library's `Entity` class but find it lacks a `heal()` method, you typically have only two options:
1. Inherit from it (but this may break the original object's type dependencies).
2. Write a utility function like `EntityHeal(entity)` (which feels unnatural to call).

In Vox, as long as the original library author allows it, you can **directly "append" methods to that class**, and calling them feels as natural as if they were natively part of it.

---

### 2. Basic Syntax: Two Steps

**Step One: The original library declares that extension is allowed**
```vox
// Code written by library author A
CanAppend class Player {
    public string name;
    private int hp;
}
```

**Step Two: You extend and append**
```vox
// Code written by game developer B
Append class Player {
    void heal(int amount) {
        other.hp += amount; // Access the original object
    }
}
```

**Calling it is extremely natural:**
```vox
Player p = new Player();
p.heal(10); // Just like calling a method of the original class
```

---

### 3. Core Rules (Must-Know)

- **Creating something from nothing? No**: If the original class doesn't have a `CanAppend` declaration, writing `Append` directly will cause an error.
- **Changing the structure? No**: Inside an `Append` block, **you can only write methods**. You cannot add fields, write constructors, write destructors, or declare signals.
- **Strong overriding? No**: If the original class has `void test()`, you cannot override it in `Append` (`override` will cause an error), nor can you write a method with the same name and parameters. This is "appending," not "intercepting."
- **Multiple appends are allowed**: You can write multiple `Append class Player` blocks, as long as the method signatures don't conflict.

---

### 4. Permission Control: How to access private members?

By default, `Append` behaves like an external class and **can only access public members**. If you forcibly access private members, the Vox compiler will immediately reject it.

If the original library author trusts your extension, they need to green-light it in `CanAppend`:

```vox
CanAppend class Player {
    private int hp;

    // Option 1: Wildcard authorization (all Append methods can access private)
    friend *; 

    // Option 2: Precise authorization (only methods with these names can access private)
    // friend heal, takeDamage; 
}
```
With `friend *;`, your appended `heal()` can directly do `other.hp += 10;`.

---

### 5. Context Keywords: `self` and `other`

Inside methods of `Append`, there are two very special context keywords:

- **`other`**: Represents the **host object** (i.e., the `Player` instance itself). If you want to read or write the original class's properties or call its methods, always use `other`.
- **`self`**: Represents the **current Append context**. It is specifically used to call **other Append methods**.

**Code Example:**
```vox
Append class Player {
    void heal(int amount) {
        other.hp += amount; 
        self.logHeal(amount); // Call another Append method
    }

    void logHeal(int amount) {
        Debug.log("Player healed " + amount + " HP");
    }
}
```
*Note: Inside `Append`, you cannot use `self.hp`. To access the original class's members, you must use `other.hp`.*

---

### 6. Inheritance and Polymorphism: How to use `super`?

If your `Player` inherits from `BaseClass`, you can still call base class methods via `super` inside `Append`.

```vox
Append class Player {
    void attack() {
        super.playSound("slash"); // Call a base class method
        Debug.log("Player attacks!"); 
    }
}
```

---

### 7. When is `Append` appropriate? (Best Practices)

- **Plugin architecture**: Your game engine core code contains only the most basic functionality. DLC or Mod authors can add a `dropLoot()` method to the `Enemy` class via `Append`.
- **Separation of concerns**: Keep only core state in the original class, and delegate serialization operations like `save()` / `load()` to `Append`.
- **Testing**: Test code can append an `assertIsValid()` method to a class without polluting the production environment.

### When should you **not** use it?
- When you need to add private fields to a class (e.g., an `isDead` flag) — `Append` absolutely cannot do this; you should use `Dict<Player, bool>` or modify the original class.
- When you need to change the behavior of an existing method (override) — you should solve this through inheritance (`class MyPlayer : Player`).

---

### Conclusion
The combination of `CanAppend` + `Append` + `friend` gives Vox extremely strong modularity while maintaining absolute type safety at compile time. Use it well, and your software architecture will become extremely decoupled!