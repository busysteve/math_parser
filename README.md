# Dynamic C++ Formula Engine

This C++20 header compiles mathematical expressions and resolves a graph of named values and formulas at runtime.

A variable does not have to be a hard-coded number. It can be another formula, which can reference more formulas:

```text
subtotal   = price * quantity
discount   = max(0, subtotal * discount_rate)
taxable    = subtotal - discount
tax        = taxable * tax_rate
grand_total = taxable + tax + shipping
```

The engine resolves dependencies automatically, caches repeated dependencies during each evaluation, reports missing variables, and detects circular references such as `a=b+1` and `b=a+1`.

## Configuration

```ini
[program]
formula = grand_total

[values]
price = 19.95
quantity = 3
tax_rate = 0.07

[variables]
subtotal = price * quantity
tax = subtotal * tax_rate
grand_total = subtotal + tax
```

`[values]` accepts strict numeric literals. `[variables]`, `[expressions]`, and `[formulas]` accept full expressions. Definitions may appear in any order.

```cpp
FormulaEngine engine;
engine.loadFromConfig("settings.ini");
std::cout << engine.evaluate() << '\n';
```

## Definitions from code

```cpp
FormulaEngine engine;

engine.setMainExpression("grand_total");
engine.setValue("price", 19.95);
engine.setValue("quantity", 3);
engine.setExpression("subtotal", "price * quantity");
engine.setExpression("tax", "subtotal * tax_rate");
engine.setExpression("grand_total", "subtotal + tax");
engine.setValue("tax_rate", 0.07);

std::cout << engine.evaluate() << '\n';
```

Dynamic `name=expression` text can be applied without knowing the name in advance:

```cpp
engine.setAssignment(runtimeText); // for example: "shipping=max(5,subtotal*.03)"
```

## Definitions from the command line

```bash
./formula_example --config settings.ini
```

Override configured values or formulas:

```bash
./formula_example --config settings.ini price=25 quantity=4
./formula_example --config settings.ini --set 'shipping=subtotal*0.10'
./formula_example --config settings.ini --value tax_rate=0.08
```

Build an entire formula graph from command-line arguments:

```bash
./formula_example \
  --formula 'area*unit_cost' \
  'area=width*height' \
  width=8 \
  height=5 \
  unit_cost=3.25
```

All bare `NAME=EXPRESSION` arguments and `--set` arguments are compiled as expressions. `--value` requires a plain number.

Command-line assignments override configuration definitions with the same name.

## Live external values

Names not defined in code/config/CLI can be supplied by a callback:

```cpp
engine.setValueResolver(
    [&](std::string_view name) -> std::optional<double> {
        return sensorDatabase.lookup(name);
    });
```

## Custom functions

```cpp
engine.registerFunction("lerp", 3, 3, [](std::span<const double> a) {
    return a[0] + (a[1] - a[0]) * a[2];
});
```

Custom functions are available to the main expression and every variable expression.

## Supported syntax

- Operators: `+ - * / % ^`
- Parentheses and unary `+`/`-`
- Scientific notation such as `1.5e-4`
- Constants: `pi`, `e`
- Functions: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `sqrt`, `abs`, `exp`, `log`, `log10`, `floor`, `ceil`, `round`, `pow`, `min`, `max`, `clamp`

## Build

```bash
cmake -S . -B build
cmake --build build
./build/formula_example --config settings.ini
```
