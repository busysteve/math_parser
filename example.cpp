#include "math_formula.hpp"

#include <iostream>
#include <span>
#include <unordered_map>
#include <vector>

int main() {
    try {
        MathFormula formula;

        // Optional application-defined function.
        formula.registerFunction("lerp", 3, 3, [](std::span<const double> a) {
            return a[0] + (a[1] - a[0]) * a[2];
        });

        // Or use:
        // formula.compileFromConfig("settings.ini", "formula");
        formula.compile("clamp(base * (1 + rate)^years + sin(t), 0, limit)");

        // Convenient evaluation using names.
        const std::unordered_map<std::string, double> variables{
            {"base", 1000.0},
            {"rate", 0.05},
            {"years", 10.0},
            {"t", 0.5},
            {"limit", 2000.0}
        };
        std::cout << "Named evaluation: " << formula.evaluate(variables) << '\n';

        // Faster repeated evaluation: resolve variable indexes once.
        std::vector<double> runtimeValues(formula.variables().size());
        const auto baseIndex = formula.variableIndex("base");
        const auto rateIndex = formula.variableIndex("rate");
        const auto yearsIndex = formula.variableIndex("years");
        const auto timeIndex = formula.variableIndex("t");
        const auto limitIndex = formula.variableIndex("limit");

        runtimeValues[baseIndex] = 1000.0;
        runtimeValues[rateIndex] = 0.05;
        runtimeValues[yearsIndex] = 10.0;
        runtimeValues[limitIndex] = 2000.0;

        for (int frame = 0; frame < 5; ++frame) {
            runtimeValues[timeIndex] = frame * 0.1;
            std::cout << "Frame " << frame << ": "
                      << formula.evaluate(runtimeValues) << '\n';
        }
    } catch (const MathFormula::CompileError& error) {
        std::cerr << "Formula error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
