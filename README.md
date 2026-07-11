# Dynamic C++ Runtime Math Formula Parser

`MathFormula` compiles an expression once and evaluates it repeatedly. Variable names are discovered from the expression; the C++ application does not need a hard-coded list.

## Configuration

```ini
formula = subtotal * (1 + tax_rate) - discount

[variables]
subtotal = 120.0
tax_rate = 0.07
discount = 10.0
```

```cpp
MathFormula formula;
formula.loadFromConfig("settings.ini");
double result = formula.evaluate();
```

## Runtime updates

Any name can be updated dynamically:

```cpp
formula.setVariable(nameFromInput, valueFromInput);
double result = formula.evaluate();
```

`loadVariablesFromConfig()` refreshes configured values without recompiling the expression.

## Live value resolver

A resolver can provide values from a sensor, database, environment, or another runtime system:

```cpp
formula.setVariableResolver(
    [&](std::string_view name) -> std::optional<double> {
        return dataSource.lookup(name);
    }
);
```

Values explicitly supplied with `setVariable()` take priority over the resolver.

## Build

```bash
cmake -S . -B build
cmake --build build
./build/formula_example settings.ini
```

Override any configured variable from the command line:

```bash
./build/formula_example settings.ini base=2000 limit=5000
```
