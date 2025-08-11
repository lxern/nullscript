# NullScript

## Building

```bash
make
```

This creates `build/nullscript`.

## Running

Interactive mode:
```bash
./build/nullscript
```

From file:
```bash
./build/nullscript program.ns
```

## Basic Syntax

### Values
```
none
nil
undefined
null
```

### Data Structures
```
pair(nil, undefined)
list(none, nil, undefined, null)
```

### Functions
```
function add_one(n) {
    pair(none, n)
}
```

### Pattern Matching
```
match value {
    case nil -> undefined
    case pair(x, y) -> x
    default -> null
}
```

### Conditionals
```
if eq(x, nil) {
    undefined
} else {
    null
}
```

## Built-in Functions

- `eq(a, b)` - Returns `nil` if equal, `undefined` otherwise
- `car(pair)` - Get first element of pair
- `cdr(pair)` - Get second element of pair
- `print(format_pair)` - Print values (see encoding below)

## Number Encoding

Numbers are encoded as nested pairs with `none`:
- `0` = `nil`
- `1` = `pair(none, nil)`
- `2` = `pair(none, pair(none, nil))`
- etc.

## Print Formatting

The `print` function takes a pair where the first element determines format:

- `pair(none, value)` - Print as number
- `pair(undefined, value)` - Print as ASCII character
- `pair(null, value)` - Print type name

Example printing "A" (ASCII 65):
```
print(pair(undefined, pair(none, pair(none, ...65 times... pair(none, nil)))))
```

## Examples

Hello world (prints "A"):
```
function hello() {
    print(pair(undefined, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, pair(none, nil)))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))))
}
hello()
```

Pattern matching example:
```
function check(x) {
    match x {
        case nil -> print(pair(null, nil))
        case pair(a, b) -> print(pair(null, pair(none, nil)))
        default -> print(pair(null, undefined))
    }
}
