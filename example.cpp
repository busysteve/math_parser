#include "math_formula.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void applyCommandLineAssignment(MathFormula& formula, std::string_view assignment) {
    const std::size_t equals = assignment.find('=');
    if (equals == std::string_view::npos) {
        throw std::invalid_argument(
            "Runtime overrides must use name=value: " + std::string(assignment));
    }

    const std::string name(assignment.substr(0, equals));
    const std::string text(assignment.substr(equals + 1));

    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size()) {
        throw std::invalid_argument("Invalid value in override: " + std::string(assignment));
    }

    // The name comes from runtime input; the program has no compiled-in list
    // of variables.
    formula.setVariable(name, value);
}
} // namespace

int main(int argc, char* argv[]) {
    try {
        const std::string configPath = argc > 1 ? argv[1] : "settings.ini";

        MathFormula formula;
        formula.loadFromConfig(configPath);

        // Optional runtime overrides are also completely dynamic:
        //   ./formula_example settings.ini subtotal=250 discount=15
        for (int i = 2; i < argc; ++i) {
            applyCommandLineAssignment(formula, argv[i]);
        }

        std::cout << "Formula: " << formula.source() << "\n";
        std::cout << "Discovered variables:\n";
        for (const std::string& name : formula.variables()) {
            std::cout << "  " << name;
            if (formula.hasVariableValue(name)) {
                std::cout << " = " << formula.variableValue(name);
            } else {
                std::cout << " = <missing>";
            }
            std::cout << '\n';
        }

        std::cout << "Result: " << formula.evaluate() << '\n';
    } catch (const MathFormula::CompileError& error) {
        std::cerr << "Formula error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
