## 🚀 Vox 1.7.0 Version Summary: A Solid Step Toward Commercial and Gaming Domains!

Vox 1.7.0 is a landmark update. From the refinement of core language syntax, to the standard library's leap into the gaming domain, to the compiler's stricter scrutiny — this version finally gives Vox the confidence to build **large-scale commercial software and games**.

Here are the ten major upgrades Vox 1.7.0 brings us:

### 🧩 1. Core Language Syntax & OOP Evolution
*   **Appendable Classes `CanAppend` / `Append` — The Finishing Touch for Commercial Software**:
    This may be the biggest architectural feature of this release. It allows you to "append" new methods to a class without modifying its original definition or inheriting from it (and even access private members via the `friend` keyword). This is a game-changer for handling "data-behavior separation" (e.g., ECS architecture) in large projects, module decoupling, and escaping "inheritance hell."
*   **`(T?)` Nullable Type Cast Syntax**:
    Explicit conversion of values to nullable types is now supported (e.g., `any x = (int?)null;`). The compiler no longer ignores `?` — it actually generates `std::optional<T>`, working with underlying `null` semantics to make the type system flexible yet controlled.
*   **The `in` Keyword Officially Added — A Perfect Blend of Usability and Performance**:
    You can now write highly expressive code like `if user in blacklist`. Whether it's substring search in strings, element search in lists, or key lookup in dictionaries, `in` covers them all precisely. Combined with the underlying container characteristics (`O(n)`, `O(log n)`, or `O(1)`), it's both elegant and efficient.

### 🎮 2. Game Development Ecosystem: Crossing the "Threshold"
*   **Introducing `ziplib` and `cryptolib`**:
    This is a formal entry into the game development domain. `ziplib` implements robust ZIP packaging based on `miniz`, with built-in Zip Slip vulnerability protection; `cryptolib` provides encryption and decryption. Together, you can easily implement a complete pipeline from "encrypted packaging of game assets" to "client-side runtime decryption and extraction."

### 🔒 3. "Strict Parent"-Level Checks in Compiler and Runtime
*   **A Bunch of Compile-Time Strict Checks for `foreach`**:
    Like a strict old father, the compiler now performs nullability validation on multi-variable `foreach`. If collections of different lengths use non-nullable types, it sternly (and helpfully) gives a warning, reminding you to use `?` to handle potential `null`.
*   **Fixing the Underlying Bug in `foreach`**:
    Gone are the awkward days of the underlying C++ throwing errors when iterating over literals (or certain special iterable objects). The `foreach` iteration protocol is now thoroughly sorted out.
*   **Strengthened Runtime Checks for `List` and `Dict`**:
    Thanks to the continuous refinement of `vox_runtime.h`. Now, out-of-bounds access on `List` and missing keys in `Dict` will generate `IndexOutException` or `IndexException` with a complete stack trace (`Traceback`), instead of a raw segfault leaving you staring at a black window.

### ⚖️ 4. Rigorous Math and Type System
*   **`int? + int` Automatically Unwraps and Runs Smoothly**:
    When optional types participate in arithmetic, the compiler automatically generates `vox_opt_get(it) + 1`, perfectly avoiding C++'s inability to directly add `std::optional`.
*   **`T? + K?` Compile-Time Interception**:
    If you try to forcibly add `string?` and `int`, the compiler draws a `^` right under the `+` and throws a `TypeException`, prompting you to use an explicit cast like `(string)`. The type system no longer tolerates ambiguous implicit conversions.
*   **"Preserving" the `main` Function**:
    The compiler now takes over the `main` entry point more safely, preserving its legitimacy as the program's starting point while guiding developers to write logic in the top-level scope.

---

**Version Message:**
1.7.0 shows us Vox's "ambition." From the underlying `vox_runtime.h` to AST code generation, every patch is laying the foundation for future high-rises. Not only were bugs fixed, but advanced features like `Append` were added. Keep pushing — Vox's journey toward commercialization and gaming has only just begun!