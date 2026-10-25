# Vox Naming Conventions & Rules

## Note
Naming conventions are only meant to make collaboration between teams/users smoother; they are not mandatory rules. After all, every company or individual has their own preferred conventions. Naming is free; conventions simply help others use your code more effectively.

## Conventions
| Type | Convention | Example |
|---|---|---|
| Ordinary variable | lowerCamelCase | `idiotLike` |
| Constant | ALL_CAPS with '_' separator | `KEY_W` |
| Class | UpperCamelCase | `EntityManager` |
| Ordinary function | lowerCamelCase | `isIdiot` |
| Private variable | '_' + lowerCamelCase | `_isIdiot` |
| Type conversion function | ALL_CAPS, no separator, 2–8 characters, use "3" instead of the word "to" | `RGB3HEX` |
| File name | lowerCamelCase | `playerMove.v` |
| Constructor | Constructor `_init_` should use `self` as the return value | `self _init_()` |
| Exception class | `XxxException` | `IdiotLikeException` |

## Rules / Agreements
| Type | Rule / Agreement | Example | Notes |
|---|---|---|---|
| Initialization folder file | `_init_.v` | `_init_.v` | - |
| Class "magic methods" | `_xxx_` | Constructor `_init_` | See [Magic Methods](Basic%20Syntax.md#Class-"Magic"-Methods) for details |
| GUI project | On first execution, run `vox run "file.v" --window` to copy some important `.dll` files | `vox run "guiTest.v" --window` | - |
| GUI project | Thereafter, run `vox run "file.v" --gui` to compile a program without a cmd window | `vox run "guiTest.v" --gui` | - |

Every project's main file must import the `Code` library at the beginning, otherwise it will directly raise a `SyntaxException`!
Try not to modify any files in the standard library. If errors occur because of this, please revert or re-download. Every file may be linked to others.
For `foreach` with multiple variables, it is best not to loop more than 4 at the same time, otherwise readability will suffer.
Do not overuse the `any` type, because it will not trigger the compiler's static compile-time checks, although the IDE can (for testing). The `any` type itself is not truly "dynamic"; it is simply a type that can hold any type. Therefore, you don't need to worry about the performance of `any`; you only need to worry about compile time 😜 — [From me ヾ(≧▽≦*)o](Any%20Type.md)