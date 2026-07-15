#include "math_formula.hpp"

#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>

namespace {
void printHelp(std::string_view executable) {
    std::cout
        << "Usage:\n"
        << "  " << executable << " --config FILE [overrides...]\n"
        << "  " << executable << " --formula EXPR NAME=EXPR ...\n\n"
        << "Options:\n"
        << "  -c, --config FILE          Load an INI configuration\n"
        << "  -f, --formula EXPR         Set/replace the main formula\n"
        << "  -s, --set NAME=EXPR        Set a value or computed variable\n"
        << "  -v, --value NAME=NUMBER    Set a strict numeric value\n"
        << "  NAME=EXPR                  Shorthand for --set NAME=EXPR\n\n"
        << "Examples:\n"
        << "  " << executable << " --config settings.ini\n"
        << "  " << executable << " --config settings.ini price=25 quantity=4\n"
        << "  " << executable
        << " --config settings.ini --set 'shipping=subtotal*0.10'\n"
        << "  " << executable
        << " --formula 'area*unit_cost' 'area=width*height' width=8 height=5 unit_cost=3.25\n";
}
} // namespace

int main(int argc, const char* argv[]) {
    try {
        if (argc == 1) {
            printHelp(argv[0]);
            return 0;
        }
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--help" ||
                std::string_view(argv[i]) == "-h") {
                printHelp(argv[0]);
                return 0;
            }
        }

        FormulaEngine engine;

        // Application-defined functions work inside the main formula and all
        // variable expressions, regardless of whether they came from code,
        // configuration, or command line.
        engine.registerFunction("lerp", 3, 3, [](std::span<const double> a) {
            return a[0] + (a[1] - a[0]) * a[2];
        });

        // Code-created expressions are dynamic too. A config or CLI assignment
        // with the same name replaces this definition.
        engine.setExpression("service_fee", "subtotal * 0.015");

        // Parses --config, --formula, --set, --value, and bare NAME=EXPR.
        // Config is loaded first so command-line assignments always win.
        engine.applyCommandLine(argc, argv);

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Formula: " << engine.mainSource() << '\n';
        std::cout << "Result:  " << engine.evaluate() << '\n';

        // Any named expression can also be evaluated directly.
        if (engine.contains("subtotal")) {
            std::cout << "Subtotal: " << engine.evaluateName("subtotal") << '\n';
        }

        return 0;
    } catch (const MathFormula::CompileError& error) {
        std::cerr << "Formula syntax error: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
