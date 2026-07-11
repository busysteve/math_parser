#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class MathFormula {
public:
    class CompileError : public std::runtime_error {
    public:
        CompileError(std::size_t position, const std::string& message)
            : std::runtime_error(message + " at character " + std::to_string(position)),
              position_(position) {}

        [[nodiscard]] std::size_t position() const noexcept { return position_; }

    private:
        std::size_t position_;
    };

    class EvaluationError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    using NativeFunction = std::function<double(std::span<const double>)>;
    using VariableMap = std::unordered_map<std::string, double>;
    using VariableResolver = std::function<std::optional<double>(std::string_view)>;

    MathFormula() {
        registerConstant("pi", std::numbers::pi_v<double>);
        registerConstant("e",  std::numbers::e_v<double>);

        registerUnary("sin",   [](double x) { return std::sin(x); });
        registerUnary("cos",   [](double x) { return std::cos(x); });
        registerUnary("tan",   [](double x) { return std::tan(x); });
        registerUnary("asin",  [](double x) { return std::asin(x); });
        registerUnary("acos",  [](double x) { return std::acos(x); });
        registerUnary("atan",  [](double x) { return std::atan(x); });
        registerUnary("sinh",  [](double x) { return std::sinh(x); });
        registerUnary("cosh",  [](double x) { return std::cosh(x); });
        registerUnary("tanh",  [](double x) { return std::tanh(x); });
        registerUnary("sqrt",  [](double x) { return std::sqrt(x); });
        registerUnary("abs",   [](double x) { return std::abs(x); });
        registerUnary("exp",   [](double x) { return std::exp(x); });
        registerUnary("log",   [](double x) { return std::log(x); });
        registerUnary("log10", [](double x) { return std::log10(x); });
        registerUnary("floor", [](double x) { return std::floor(x); });
        registerUnary("ceil",  [](double x) { return std::ceil(x); });
        registerUnary("round", [](double x) { return std::round(x); });

        registerFunction("pow", 2, 2, [](std::span<const double> a) {
            return std::pow(a[0], a[1]);
        });
        registerFunction("atan2", 2, 2, [](std::span<const double> a) {
            return std::atan2(a[0], a[1]);
        });
        registerFunction("min", 1, unlimitedArguments(), [](std::span<const double> a) {
            return *std::min_element(a.begin(), a.end());
        });
        registerFunction("max", 1, unlimitedArguments(), [](std::span<const double> a) {
            return *std::max_element(a.begin(), a.end());
        });
        registerFunction("clamp", 3, 3, [](std::span<const double> a) {
            if (a[1] > a[2]) {
                throw EvaluationError("clamp(): lower bound is greater than upper bound");
            }
            return std::clamp(a[0], a[1], a[2]);
        });
    }

    static constexpr std::size_t unlimitedArguments() noexcept {
        return std::numeric_limits<std::size_t>::max();
    }

    void registerConstant(std::string name, double value) {
        validateIdentifier(name);
        constants_[std::move(name)] = value;
    }

    void registerFunction(
        std::string name,
        std::size_t minimumArguments,
        std::size_t maximumArguments,
        NativeFunction function) {

        validateIdentifier(name);
        if (!function) {
            throw std::invalid_argument("Function callback cannot be empty");
        }
        if (minimumArguments > maximumArguments) {
            throw std::invalid_argument("Minimum argument count exceeds maximum argument count");
        }

        Function entry{
            .name = name,
            .minimumArguments = minimumArguments,
            .maximumArguments = maximumArguments,
            .callback = std::move(function)
        };

        const auto existing = functionLookup_.find(name);
        if (existing != functionLookup_.end()) {
            functions_[existing->second] = std::move(entry);
            return;
        }

        functionLookup_[name] = functions_.size();
        functions_.push_back(std::move(entry));
    }

    void compile(std::string expression) {
        std::vector<Instruction> newCode;
        std::vector<std::string> newVariableNames;
        std::unordered_map<std::string, std::size_t> newVariableLookup;

        Parser parser(
            *this,
            std::move(expression),
            newCode,
            newVariableNames,
            newVariableLookup);
        parser.parse();

        code_ = std::move(newCode);
        variableNames_ = std::move(newVariableNames);
        variableLookup_ = std::move(newVariableLookup);
        source_ = parser.source();
        maximumStackDepth_ = calculateMaximumStackDepth();
    }

    // Loads both the expression and all variable values from a configuration file.
    //
    // Example:
    //   formula = subtotal * (1 + tax_rate) - discount
    //
    //   [variables]
    //   subtotal = 120.0
    //   tax_rate = 0.07
    //   discount = 10.0
    //
    // Variables may also be written as var.name=value or variable.name=value.
    void loadFromConfig(
        const std::filesystem::path& file,
        std::string_view formulaKey = "formula") {

        ConfigurationData configuration = readConfiguration(file, formulaKey, true);
        compile(std::move(configuration.formula));
        setVariables(std::move(configuration.variables), true);
    }

    // Backward-compatible name. It now loads configured variable values too.
    void compileFromConfig(
        const std::filesystem::path& file,
        std::string_view key = "formula") {
        loadFromConfig(file, key);
    }

    // Reloads only values, leaving the compiled expression untouched. This is
    // useful when a configuration file changes while the program is running.
    void loadVariablesFromConfig(
        const std::filesystem::path& file,
        bool replaceExisting = false,
        std::string_view formulaKey = "formula") {

        ConfigurationData configuration = readConfiguration(file, formulaKey, false);
        setVariables(std::move(configuration.variables), replaceExisting);
    }

    void setVariable(std::string name, double value) {
        validateVariableName(name);
        runtimeVariables_[std::move(name)] = value;
    }

    void setVariables(VariableMap values, bool replaceExisting = false) {
        for (const auto& [name, value] : values) {
            (void)value;
            validateVariableName(name);
        }

        if (replaceExisting) {
            runtimeVariables_ = std::move(values);
            return;
        }

        for (auto& [name, value] : values) {
            runtimeVariables_[std::move(name)] = value;
        }
    }

    [[nodiscard]] bool hasVariableValue(std::string_view name) const {
        return runtimeVariables_.find(std::string(name)) != runtimeVariables_.end();
    }

    [[nodiscard]] double variableValue(std::string_view name) const {
        const auto found = runtimeVariables_.find(std::string(name));
        if (found == runtimeVariables_.end()) {
            throw std::out_of_range("No runtime value exists for variable: " + std::string(name));
        }
        return found->second;
    }

    bool removeVariableValue(std::string_view name) {
        return runtimeVariables_.erase(std::string(name)) != 0;
    }

    void clearVariableValues() noexcept { runtimeVariables_.clear(); }

    [[nodiscard]] const VariableMap& variableValues() const noexcept {
        return runtimeVariables_;
    }

    // A resolver can fetch values from sensors, a database, environment
    // variables, a scripting system, or any other runtime source. It is used
    // only when a value is not present in runtimeVariables_.
    void setVariableResolver(VariableResolver resolver) {
        variableResolver_ = std::move(resolver);
    }

    void clearVariableResolver() { variableResolver_ = {}; }

    // Evaluates with values previously loaded from configuration or supplied
    // through setVariable()/setVariables(). No variable names are hard-coded
    // by the caller.
    [[nodiscard]] double evaluate() const {
        return evaluateResolved([this](std::string_view name) -> std::optional<double> {
            const auto found = runtimeVariables_.find(std::string(name));
            if (found != runtimeVariables_.end()) {
                return found->second;
            }
            if (variableResolver_) {
                return variableResolver_(name);
            }
            return std::nullopt;
        });
    }

    // Evaluates directly against any dynamic value source.
    [[nodiscard]] double evaluate(const VariableResolver& resolver) const {
        if (!resolver) {
            throw std::invalid_argument("Variable resolver cannot be empty");
        }
        return evaluateResolved(resolver);
    }

    [[nodiscard]] double evaluate(std::span<const double> variableValues) const {
        if (code_.empty()) {
            throw EvaluationError("No formula has been compiled");
        }
        if (variableValues.size() != variableNames_.size()) {
            throw EvaluationError(
                "Expected " + std::to_string(variableNames_.size()) +
                " variables, received " + std::to_string(variableValues.size()));
        }

        std::vector<double> stack;
        stack.reserve(maximumStackDepth_);

        const auto pop = [&stack]() -> double {
            if (stack.empty()) {
                throw EvaluationError("Internal evaluation stack underflow");
            }
            const double value = stack.back();
            stack.pop_back();
            return value;
        };

        for (const Instruction& instruction : code_) {
            switch (instruction.operation) {
            case Operation::PushConstant:
                stack.push_back(instruction.literal);
                break;

            case Operation::PushVariable:
                stack.push_back(variableValues[instruction.argument]);
                break;

            case Operation::Negate:
                stack.push_back(-pop());
                break;

            case Operation::Add: {
                const double right = pop();
                const double left = pop();
                stack.push_back(left + right);
                break;
            }
            case Operation::Subtract: {
                const double right = pop();
                const double left = pop();
                stack.push_back(left - right);
                break;
            }
            case Operation::Multiply: {
                const double right = pop();
                const double left = pop();
                stack.push_back(left * right);
                break;
            }
            case Operation::Divide: {
                const double right = pop();
                const double left = pop();
                if (right == 0.0) {
                    throw EvaluationError("Division by zero");
                }
                stack.push_back(left / right);
                break;
            }
            case Operation::Modulo: {
                const double right = pop();
                const double left = pop();
                if (right == 0.0) {
                    throw EvaluationError("Modulo by zero");
                }
                stack.push_back(std::fmod(left, right));
                break;
            }
            case Operation::Power: {
                const double right = pop();
                const double left = pop();
                stack.push_back(std::pow(left, right));
                break;
            }
            case Operation::Call: {
                const Function& function = functions_[instruction.argument];
                const std::size_t count = instruction.argumentCount;
                if (stack.size() < count) {
                    throw EvaluationError("Internal function-call stack underflow");
                }

                const std::size_t first = stack.size() - count;
                const std::span<const double> arguments(stack.data() + first, count);
                const double result = function.callback(arguments);
                stack.resize(first);
                stack.push_back(result);
                break;
            }
            }
        }

        if (stack.size() != 1) {
            throw EvaluationError("Internal evaluator error: invalid final stack size");
        }
        return stack.back();
    }

    [[nodiscard]] double evaluate(const VariableMap& variables) const {

        std::vector<double> orderedValues(variableNames_.size());
        for (std::size_t i = 0; i < variableNames_.size(); ++i) {
            const auto found = variables.find(variableNames_[i]);
            if (found == variables.end()) {
                throw EvaluationError("Missing variable: " + variableNames_[i]);
            }
            orderedValues[i] = found->second;
        }
        return evaluate(orderedValues);
    }

    [[nodiscard]] std::vector<std::string> missingVariables() const {
        std::vector<std::string> missing;
        for (const std::string& name : variableNames_) {
            if (runtimeVariables_.find(name) != runtimeVariables_.end()) {
                continue;
            }
            if (variableResolver_ && variableResolver_(name).has_value()) {
                continue;
            }
            missing.push_back(name);
        }
        return missing;
    }

    [[nodiscard]] std::size_t variableIndex(std::string_view name) const {
        const auto found = variableLookup_.find(std::string(name));
        if (found == variableLookup_.end()) {
            throw std::out_of_range("Formula does not contain variable: " + std::string(name));
        }
        return found->second;
    }

    [[nodiscard]] const std::vector<std::string>& variables() const noexcept {
        return variableNames_;
    }

    [[nodiscard]] const std::string& source() const noexcept { return source_; }

private:
    struct ConfigurationData {
        std::string formula;
        VariableMap variables;
    };

    [[nodiscard]] double evaluateResolved(const VariableResolver& resolver) const {
        if (code_.empty()) {
            throw EvaluationError("No formula has been compiled");
        }

        std::vector<double> orderedValues;
        orderedValues.reserve(variableNames_.size());
        for (const std::string& name : variableNames_) {
            const std::optional<double> value = resolver(name);
            if (!value.has_value()) {
                throw EvaluationError("Missing variable: " + name);
            }
            orderedValues.push_back(*value);
        }
        return evaluate(orderedValues);
    }

    enum class Operation : std::uint8_t {
        PushConstant,
        PushVariable,
        Negate,
        Add,
        Subtract,
        Multiply,
        Divide,
        Modulo,
        Power,
        Call
    };

    struct Instruction {
        Operation operation{};
        double literal{};
        std::size_t argument{};
        std::size_t argumentCount{};
    };

    struct Function {
        std::string name;
        std::size_t minimumArguments{};
        std::size_t maximumArguments{};
        NativeFunction callback;
    };

    enum class TokenType : std::uint8_t {
        End,
        Number,
        Identifier,
        Plus,
        Minus,
        Star,
        Slash,
        Percent,
        Caret,
        LeftParenthesis,
        RightParenthesis,
        Comma
    };

    struct Token {
        TokenType type{TokenType::End};
        std::string text;
        double number{};
        std::size_t position{};
    };

    class Lexer {
    public:
        explicit Lexer(const std::string& source) : source_(source) {}

        [[nodiscard]] Token next() {
            skipWhitespace();
            if (position_ >= source_.size()) {
                return Token{.type = TokenType::End, .text = {}, .position = position_};
            }

            const std::size_t start = position_;
            const char character = source_[position_];

            switch (character) {
            case '+': ++position_; return simple(TokenType::Plus, start);
            case '-': ++position_; return simple(TokenType::Minus, start);
            case '*': ++position_; return simple(TokenType::Star, start);
            case '/': ++position_; return simple(TokenType::Slash, start);
            case '%': ++position_; return simple(TokenType::Percent, start);
            case '^': ++position_; return simple(TokenType::Caret, start);
            case '(': ++position_; return simple(TokenType::LeftParenthesis, start);
            case ')': ++position_; return simple(TokenType::RightParenthesis, start);
            case ',': ++position_; return simple(TokenType::Comma, start);
            default: break;
            }

            if (std::isdigit(static_cast<unsigned char>(character)) || character == '.') {
                return number();
            }
            if (isIdentifierStart(character)) {
                return identifier();
            }

            throw CompileError(start, std::string("Unexpected character '") + character + "'");
        }

    private:
        [[nodiscard]] Token simple(TokenType type, std::size_t position) const {
            return Token{.type = type, .text = {}, .position = position};
        }

        [[nodiscard]] Token number() {
            const std::size_t start = position_;
            bool digitsBeforeDecimal = false;
            bool digitsAfterDecimal = false;

            while (position_ < source_.size() &&
                   std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                digitsBeforeDecimal = true;
                ++position_;
            }

            if (position_ < source_.size() && source_[position_] == '.') {
                ++position_;
                while (position_ < source_.size() &&
                       std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                    digitsAfterDecimal = true;
                    ++position_;
                }
            }

            if (!digitsBeforeDecimal && !digitsAfterDecimal) {
                throw CompileError(start, "Expected a number after '.'");
            }

            if (position_ < source_.size() &&
                (source_[position_] == 'e' || source_[position_] == 'E')) {
                const std::size_t exponentPosition = position_++;
                if (position_ < source_.size() &&
                    (source_[position_] == '+' || source_[position_] == '-')) {
                    ++position_;
                }

                const std::size_t exponentDigits = position_;
                while (position_ < source_.size() &&
                       std::isdigit(static_cast<unsigned char>(source_[position_]))) {
                    ++position_;
                }
                if (position_ == exponentDigits) {
                    throw CompileError(exponentPosition, "Malformed exponent");
                }
            }

            const std::string text = source_.substr(start, position_ - start);
            try {
                return Token{
                    .type = TokenType::Number,
                    .text = text,
                    .number = std::stod(text),
                    .position = start
                };
            } catch (const std::exception&) {
                throw CompileError(start, "Invalid numeric literal");
            }
        }

        [[nodiscard]] Token identifier() {
            const std::size_t start = position_++;
            while (position_ < source_.size() && isIdentifierPart(source_[position_])) {
                ++position_;
            }
            return Token{
                .type = TokenType::Identifier,
                .text = source_.substr(start, position_ - start),
                .position = start
            };
        }

        void skipWhitespace() {
            while (position_ < source_.size() &&
                   std::isspace(static_cast<unsigned char>(source_[position_]))) {
                ++position_;
            }
        }

        static bool isIdentifierStart(char c) {
            const unsigned char value = static_cast<unsigned char>(c);
            return std::isalpha(value) || c == '_';
        }

        static bool isIdentifierPart(char c) {
            const unsigned char value = static_cast<unsigned char>(c);
            return std::isalnum(value) || c == '_';
        }

        const std::string& source_;
        std::size_t position_{};
    };

    class Parser {
    public:
        Parser(
            const MathFormula& owner,
            std::string source,
            std::vector<Instruction>& code,
            std::vector<std::string>& variableNames,
            std::unordered_map<std::string, std::size_t>& variableLookup)
            : owner_(owner),
              source_(std::move(source)),
              lexer_(source_),
              code_(code),
              variableNames_(variableNames),
              variableLookup_(variableLookup) {
            advance();
        }

        void parse() {
            if (current_.type == TokenType::End) {
                throw CompileError(0, "Formula is empty");
            }
            parseExpression();
            expect(TokenType::End, "Unexpected token after the end of the formula");
        }

        [[nodiscard]] const std::string& source() const noexcept { return source_; }

    private:
        void parseExpression() { parseAdditive(); }

        void parseAdditive() {
            parseMultiplicative();
            while (current_.type == TokenType::Plus || current_.type == TokenType::Minus) {
                const TokenType operation = current_.type;
                advance();
                parseMultiplicative();
                emit(operation == TokenType::Plus ? Operation::Add : Operation::Subtract);
            }
        }

        void parseMultiplicative() {
            parseUnary();
            while (current_.type == TokenType::Star ||
                   current_.type == TokenType::Slash ||
                   current_.type == TokenType::Percent) {
                const TokenType operation = current_.type;
                advance();
                parseUnary();
                if (operation == TokenType::Star) {
                    emit(Operation::Multiply);
                } else if (operation == TokenType::Slash) {
                    emit(Operation::Divide);
                } else {
                    emit(Operation::Modulo);
                }
            }
        }

        void parseUnary() {
            if (match(TokenType::Plus)) {
                parseUnary();
                return;
            }
            if (match(TokenType::Minus)) {
                parseUnary();
                emit(Operation::Negate);
                return;
            }
            parsePower();
        }

        // Right associative: 2^3^2 == 2^(3^2). Unary minus has lower
        // precedence on the left, so -2^2 == -(2^2), but 2^-2 is valid.
        void parsePower() {
            parsePrimary();
            if (match(TokenType::Caret)) {
                parseUnary();
                emit(Operation::Power);
            }
        }

        void parsePrimary() {
            if (current_.type == TokenType::Number) {
                code_.push_back(Instruction{
                    .operation = Operation::PushConstant,
                    .literal = current_.number
                });
                advance();
                return;
            }

            if (current_.type == TokenType::Identifier) {
                const Token identifier = current_;
                advance();

                if (match(TokenType::LeftParenthesis)) {
                    parseFunctionCall(identifier);
                    return;
                }

                const auto constant = owner_.constants_.find(identifier.text);
                if (constant != owner_.constants_.end()) {
                    code_.push_back(Instruction{
                        .operation = Operation::PushConstant,
                        .literal = constant->second
                    });
                    return;
                }

                code_.push_back(Instruction{
                    .operation = Operation::PushVariable,
                    .argument = variableIndex(identifier.text)
                });
                return;
            }

            if (match(TokenType::LeftParenthesis)) {
                parseExpression();
                expect(TokenType::RightParenthesis, "Expected ')'");
                advance();
                return;
            }

            throw CompileError(current_.position, "Expected a number, variable, function, or '('");
        }

        void parseFunctionCall(const Token& identifier) {
            const auto found = owner_.functionLookup_.find(identifier.text);
            if (found == owner_.functionLookup_.end()) {
                throw CompileError(identifier.position, "Unknown function '" + identifier.text + "'");
            }

            std::size_t argumentCount = 0;
            if (current_.type != TokenType::RightParenthesis) {
                for (;;) {
                    parseExpression();
                    ++argumentCount;
                    if (!match(TokenType::Comma)) {
                        break;
                    }
                }
            }

            expect(TokenType::RightParenthesis, "Expected ')' after function arguments");
            advance();

            const Function& function = owner_.functions_[found->second];
            if (argumentCount < function.minimumArguments ||
                argumentCount > function.maximumArguments) {
                std::ostringstream message;
                message << "Function '" << function.name << "' expects ";
                if (function.minimumArguments == function.maximumArguments) {
                    message << function.minimumArguments;
                } else if (function.maximumArguments == unlimitedArguments()) {
                    message << "at least " << function.minimumArguments;
                } else {
                    message << function.minimumArguments << " to " << function.maximumArguments;
                }
                message << " argument(s), received " << argumentCount;
                throw CompileError(identifier.position, message.str());
            }

            code_.push_back(Instruction{
                .operation = Operation::Call,
                .argument = found->second,
                .argumentCount = argumentCount
            });
        }

        [[nodiscard]] std::size_t variableIndex(const std::string& name) {
            const auto found = variableLookup_.find(name);
            if (found != variableLookup_.end()) {
                return found->second;
            }

            const std::size_t index = variableNames_.size();
            variableLookup_[name] = index;
            variableNames_.push_back(name);
            return index;
        }

        bool match(TokenType type) {
            if (current_.type != type) {
                return false;
            }
            advance();
            return true;
        }

        void expect(TokenType type, const std::string& message) const {
            if (current_.type != type) {
                throw CompileError(current_.position, message);
            }
        }

        void advance() { current_ = lexer_.next(); }

        void emit(Operation operation) {
            code_.push_back(Instruction{.operation = operation});
        }

        const MathFormula& owner_;
        std::string source_;
        Lexer lexer_;
        Token current_;
        std::vector<Instruction>& code_;
        std::vector<std::string>& variableNames_;
        std::unordered_map<std::string, std::size_t>& variableLookup_;
    };

    template <typename FunctionType>
    void registerUnary(std::string name, FunctionType function) {
        registerFunction(
            std::move(name),
            1,
            1,
            [function = std::move(function)](std::span<const double> values) {
                return function(values[0]);
            });
    }

    static ConfigurationData readConfiguration(
        const std::filesystem::path& file,
        std::string_view formulaKey,
        bool requireFormula) {

        std::ifstream input(file);
        if (!input) {
            throw std::runtime_error("Could not open configuration file: " + file.string());
        }

        ConfigurationData result;
        bool formulaFound = false;
        std::string section;
        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(input, line)) {
            ++lineNumber;
            std::string_view text = trim(line);
            if (text.empty() || text.front() == '#' || text.front() == ';') {
                continue;
            }

            if (text.size() >= 2 && text.front() == '[' && text.back() == ']') {
                section = std::string(trim(text.substr(1, text.size() - 2)));
                continue;
            }

            const std::size_t equals = text.find('=');
            if (equals == std::string_view::npos) {
                throw std::runtime_error(
                    "Expected key=value in " + file.string() +
                    " at line " + std::to_string(lineNumber));
            }

            std::string key(trim(text.substr(0, equals)));
            std::string value(unquote(trim(text.substr(equals + 1))));

            if (key == formulaKey) {
                result.formula = std::move(value);
                formulaFound = true;
                continue;
            }

            std::string variableName;
            if (section == "variables") {
                variableName = std::move(key);
            } else if (key.starts_with("var.")) {
                variableName = key.substr(4);
            } else if (key.starts_with("variable.")) {
                variableName = key.substr(9);
            } else {
                // Ignore unrelated application settings.
                continue;
            }

            validateIdentifier(variableName);
            result.variables[std::move(variableName)] =
                parseConfiguredNumber(value, file, lineNumber);
        }

        if (requireFormula && !formulaFound) {
            throw std::runtime_error(
                "Configuration key '" + std::string(formulaKey) +
                "' was not found in " + file.string());
        }
        return result;
    }

    static std::string_view unquote(std::string_view value) {
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value.remove_prefix(1);
            value.remove_suffix(1);
        }
        return value;
    }

    static double parseConfiguredNumber(
        std::string_view text,
        const std::filesystem::path& file,
        std::size_t lineNumber) {

        const std::string value(trim(text));
        try {
            std::size_t consumed = 0;
            const double number = std::stod(value, &consumed);
            if (consumed != value.size()) {
                throw std::invalid_argument("trailing characters");
            }
            return number;
        } catch (const std::exception&) {
            throw std::runtime_error(
                "Invalid numeric variable value '" + value + "' in " +
                file.string() + " at line " + std::to_string(lineNumber));
        }
    }

    void validateVariableName(std::string_view name) const {
        validateIdentifier(name);
        if (constants_.find(std::string(name)) != constants_.end()) {
            throw std::invalid_argument(
                "Variable name conflicts with a registered constant: " + std::string(name));
        }
    }

    static void validateIdentifier(std::string_view name) {
        if (name.empty() ||
            !(std::isalpha(static_cast<unsigned char>(name.front())) || name.front() == '_')) {
            throw std::invalid_argument("Invalid identifier: " + std::string(name));
        }
        for (const char character : name.substr(1)) {
            const unsigned char value = static_cast<unsigned char>(character);
            if (!std::isalnum(value) && character != '_') {
                throw std::invalid_argument("Invalid identifier: " + std::string(name));
            }
        }
    }

    static std::string_view trim(std::string_view text) {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
            text.remove_prefix(1);
        }
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
            text.remove_suffix(1);
        }
        return text;
    }

    [[nodiscard]] std::size_t calculateMaximumStackDepth() const {
        std::size_t depth = 0;
        std::size_t maximum = 0;

        for (const Instruction& instruction : code_) {
            switch (instruction.operation) {
            case Operation::PushConstant:
            case Operation::PushVariable:
                ++depth;
                break;
            case Operation::Negate:
                break;
            case Operation::Add:
            case Operation::Subtract:
            case Operation::Multiply:
            case Operation::Divide:
            case Operation::Modulo:
            case Operation::Power:
                --depth; // two operands become one result
                break;
            case Operation::Call:
                depth = depth - instruction.argumentCount + 1;
                break;
            }
            maximum = std::max(maximum, depth);
        }
        return maximum;
    }

    std::unordered_map<std::string, double> constants_;
    std::vector<Function> functions_;
    std::unordered_map<std::string, std::size_t> functionLookup_;

    std::string source_;
    std::vector<Instruction> code_;
    std::vector<std::string> variableNames_;
    std::unordered_map<std::string, std::size_t> variableLookup_;
    std::size_t maximumStackDepth_{};

    VariableMap runtimeVariables_;
    VariableResolver variableResolver_;
};

