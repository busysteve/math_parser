#include "math_formula.hpp"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace {
bool close(double a, double b) {
    return std::abs(a - b) < 1e-12;
}
}

int main() {
    {
        FormulaEngine engine;
        engine.setMainExpression("grand_total");
        engine.setExpression("grand_total", "subtotal + tax");
        engine.setExpression("tax", "subtotal * tax_rate");
        engine.setExpression("subtotal", "price * quantity");
        engine.setValue("price", 10.0);
        engine.setExpression("quantity", "2 + 3");
        engine.setValue("tax_rate", 0.10);
        assert(close(engine.evaluate(), 55.0));
    }

    {
        FormulaEngine engine;
        engine.setMainExpression("x");
        engine.setExpression("x", "external * 2");
        engine.setValueResolver([](std::string_view name) -> std::optional<double> {
            if (name == "external") {
                return 21.0;
            }
            return std::nullopt;
        });
        assert(close(engine.evaluate(), 42.0));
    }

    {
        FormulaEngine engine;
        engine.setMainExpression("a");
        engine.setExpression("a", "b + 1");
        engine.setExpression("b", "a + 1");
        bool detected = false;
        try {
            (void)engine.evaluate();
        } catch (const FormulaEngine::Error& error) {
            detected = std::string(error.what()).find("Circular") != std::string::npos;
        }
        assert(detected);
    }

    {
        FormulaEngine engine;
        engine.registerFunction("twice", 1, 1, [](std::span<const double> a) {
            return a[0] * 2.0;
        });
        engine.setExpression("base", "20 + 1");
        engine.setMainExpression("twice(base)");
        assert(close(engine.evaluate(), 42.0));
    }

    {
        const std::filesystem::path file =
            std::filesystem::temp_directory_path() / "formula_engine_test.ini";
        {
            std::ofstream output(file);
            output
                << "[program]\n"
                << "formula = total\n"
                << "[values]\n"
                << "price = 10\n"
                << "[variables]\n"
                << "quantity = 2 + 3\n"
                << "subtotal = price * quantity\n"
                << "total = subtotal * 1.1\n";
        }

        FormulaEngine engine;
        engine.loadFromConfig(file);
        assert(close(engine.evaluate(), 55.0));
        std::filesystem::remove(file);
    }

    return 0;
}
