# Dynamic C++ Math Formula Parser

`MathFormula` compiles expressions once and evaluates them repeatedly. The top-level formula and the variables it uses are discovered dynamically; the application does not need a hard-coded variable list.

## Formula-valued variables

Every entry under `[variables]` may be a literal number or another formula:

```ini
formula = grand_total

[variables]
unit_price = 19.95
quantity = 3
subtotal = unit_price * quantity
tax_rate = 0.07
tax = subtotal * tax_rate
shipping = max(4.99, subtotal * 0.05)
grand_total = subtotal + tax + shipping
```

```cpp
MathFormula formula;
formula.loadFromConfig("settings.ini");
const double result = formula.evaluate();
```

Dependencies are resolved automatically. Formula-valued variables are evaluated lazily and cached for the duration of each `evaluate()` call.

## Runtime formulas and values

```cpp
formula.setVariable("quantity", 5.0);
formula.setVariableFormula("discount", "subtotal * 0.10");
formula.compile("grand_total - discount");

const double result = formula.evaluate();
```

A direct value replaces a formula with the same name, and a formula replaces a direct value with the same name.

Runtime values may override configured values or formulas for one evaluation:

```cpp
MathFormula::VariableMap overrides{
    {"quantity", 10.0},
    {"tax_rate", 0.08}
};

const double result = formula.evaluate(overrides);
```

## Live value resolver

A resolver can provide otherwise undefined values from sensors, databases, environment variables, or application state:

```cpp
formula.setVariableResolver(
    [](std::string_view name) -> std::optional<double> {
        if (name == "temperature") {
            return readTemperatureSensor();
        }
        return std::nullopt;
    });
```

Resolution priority is:

1. Per-call runtime overrides
2. Stored numeric values
3. Stored formula-valued variables
4. Live resolver

## Circular dependencies

Cycles are rejected during evaluation:

```cpp
formula.setVariableFormula("a", "b + 1");
formula.setVariableFormula("b", "a + 1");
```

This produces an error such as:

```text
Circular variable formula dependency: a -> b -> a
```

## Build

```bash
cmake -S . -B build
cmake --build build
./build/math_formula_example settings.ini
```
