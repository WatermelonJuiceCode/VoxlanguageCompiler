# The `in` Keyword

In the Vox language, `in` is a very frequently used and elegant keyword. It has two core roles: **first, as the "compass" of `foreach` loops for iterating over data; second, as a "containment" operator for checking whether an element exists in a container.**

Below is a comprehensive introduction to the various uses of the `in` syntax.

---

### I. The Art of Iteration: `in` in `foreach`

Vox's `foreach` loop connects iteration variables to iterable objects via the `in` keyword.

#### 1. Basic Iteration (Single Variable)
This is the most common usage, iterating over lists, dictionaries, strings, etc.:

```vox
List<string> fruits = ["apple", "banana", "orange"];

// Iterate over a list (elements)
foreach (string fruit; fruit in fruits) {
    Debug.log("I like eating: " + fruit);
}

// Iterate over a dictionary (iterates Keys by default)
Dict<string, int> ages = {"Alice": 20, "Bob": 25};
foreach (string name; name in ages) {
    Debug.log(name + "'s age is " + string(ages[name]));
}

// Iterate over a string (at the UTF-8 character level)
string text = "HelloVox";
foreach (string ch; ch in text) {
    Debug.log(ch); // Outputs H, e, l, l, o, V, o, x in sequence
}
```

#### 2. Multi-Variable Parallel Iteration (Advanced)
Vox supports iterating over multiple collections in a single `foreach` (**the loop count is determined by the longest collection**):

```vox
// Iterate over two lists simultaneously
foreach (int i, int j; i in range(10), j in range(15)) {
    Debug.log(i, j);
}
```
> **⚠️ Note**: When the collections have different lengths, the shorter collection will be "exhausted." If the type is non-nullable (e.g., `int`), the default value (`0`) is used instead, and the compiler emits a warning. For safety, using nullable types `?` is recommended:
> ```vox
> // The first 10 times print normally; the last 5 times i becomes null
> foreach (int? i, int j; i in range(10), j in range(15)) {
>     Debug.log(i, j); 
> }
> ```

#### 3. Making Custom Classes Support `in` Iteration
If you want your own class to be iterable via `foreach`, simply implement the iteration protocol in the class: `_begin_()`, `_done_()`, `_next_()`, `_current_()`.

---

### II. The Art of Judgment: `in` as a Membership Operator

Similar to Python, Vox's `in` can be used as a boolean operator to check whether a value exists in a target container. Internally, it is translated to `vox_in(left, right)`.

#### 1. String Search (Substring)
```vox
string sentence = "Vox is a modern language";
if ("modern" in sentence) {
    Debug.log("Found 'modern'");
}
```

#### 2. List / Set Search (Element)
```vox
List<int> nums = [10, 20, 30, 40];
if (20 in nums) {
    Debug.log("The list contains 20");
}
```

#### 3. Dictionary Search (Key)
```vox
Dict<string, int> scores = {"Tom": 95, "Jerry": 88};
if ("Tom" in scores) {
    Debug.log("Tom is in the dictionary");
}
```
> **Tip**: The `in` operator for dictionaries only checks **Keys**, not Values.

#### 4. Generic / Custom Type Search
The underlying `vox_in` provides a SFINAE fallback: as long as your container (including the `Any` type) has a `contains` method, the `in` syntax seamlessly supports it.

---

### III. Pitfall Guide

1. **Distinguish `in` from `==`**: `in` checks "whether the container contains it," while `==` checks "whether they are equal."
2. **Don't confuse `foreach`'s `in` with the logical operator `in`**:
   * The `in` in `foreach` is a syntactic structure (declaring the loop body).
   * The `in` in `if ("a" in list)` is a boolean expression.
3. **Performance reminder**: If you frequently use `in` (`item in List`) on large lists, since it is internally an `O(n)` traversal, for extremely performance-sensitive scenarios, consider switching to `Dict` (internally `O(log n)` if it's a `map`, or `O(1)` if it's an `unordered_map`).

The `in` syntax makes Vox code read like natural English (e.g., `if user in blacklist`), elegant and powerful!