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
#include <unordered_set>
#include <utility>
#include <vector>

// Compiles one mathematical expression into compact stack-machine bytecode.
// Use FormulaEngine below when named variables may themselves be expressions.
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

    MathFormula() {
        registerConstant("pi", std::numbers::pi_v<double>);
        registerConstant("e", std::numbers::e_v<double>);

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

    [[nodiscard]] double evaluate(
        const std::unordered_map<std::string, double>& variables) const {

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

        // Right associative: 2^3^2 == 2^(3^2).
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
                --depth;
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
};

// Manages a graph of named values and named formulas.
//
// Resolution order for a name:
//   1. Explicit literal set with setValue()
//   2. Expression set with setExpression()
//   3. Optional external resolver callback
//
// Values and formulas replace one another by name, making later config/CLI
// assignments natural overrides. Circular references are detected at runtime.
class FormulaEngine {
public:
    using NativeFunction = MathFormula::NativeFunction;
    using ValueResolver = std::function<std::optional<double>(std::string_view)>;

    class Error : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    FormulaEngine() = default;

    void setMainExpression(std::string expression) {
        main_ = compileExpression(std::move(expression));
    }

    void setValue(std::string name, double value) {
        validateIdentifier(name);
        expressions_.erase(name);
        values_[std::move(name)] = value;
    }

    // The right-hand side may be a literal ("42") or an arbitrary formula.
    void setExpression(std::string name, std::string expression) {
        validateIdentifier(name);
        CompiledExpression compiled = compileExpression(std::move(expression));
        values_.erase(name);
        expressions_[std::move(name)] = std::move(compiled);
    }

    // Convenience for dynamic name=expression input.
    void setAssignment(std::string_view assignment) {
        const auto [name, expression] = splitAssignment(assignment);
        setExpression(name, expression);
    }

    void erase(std::string_view name) {
        values_.erase(std::string(name));
        expressions_.erase(std::string(name));
    }

    void clearVariables() {
        values_.clear();
        expressions_.clear();
    }

    void clear() {
        clearVariables();
        main_.reset();
    }

    [[nodiscard]] bool contains(std::string_view name) const {
        const std::string key(name);
        return values_.contains(key) || expressions_.contains(key);
    }

    void setValueResolver(ValueResolver resolver) {
        resolver_ = std::move(resolver);
    }

    void registerConstant(std::string name, double value) {
        validateIdentifier(name);
        customConstants_[name] = value;
        if (main_) {
            main_->formula.registerConstant(name, value);
            main_->formula.compile(main_->source);
        }
        for (auto& [_, expression] : expressions_) {
            expression.formula.registerConstant(name, value);
            expression.formula.compile(expression.source);
        }
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
        FunctionRegistration registration{
            .name = name,
            .minimumArguments = minimumArguments,
            .maximumArguments = maximumArguments,
            .callback = std::move(function)
        };
        customFunctions_[name] = registration;
        applyFunctionRegistration(main_, registration);
        for (auto& [_, expression] : expressions_) {
            applyFunctionRegistration(expression, registration);
        }
    }

    // INI-style configuration:
    //
    //   [program]
    //   formula = grand_total
    //
    //   [values]
    //   price = 19.95
    //
    //   [variables]
    //   subtotal = price * quantity
    //   grand_total = subtotal + tax
    //
    // [variables], [expressions], and [formulas] all accept expressions.
    // [values] requires numeric literals. A top-level "formula=" is also accepted.
    void loadFromConfig(const std::filesystem::path& file, bool clearExisting = false) {
        std::ifstream input(file);
        if (!input) {
            throw Error("Could not open configuration file: " + file.string());
        }
        if (clearExisting) {
            clear();
        }

        std::string section;
        std::string line;
        std::size_t lineNumber = 0;

        while (std::getline(input, line)) {
            ++lineNumber;
            std::string_view text = trim(line);
            if (text.empty() || text.front() == '#' || text.front() == ';') {
                continue;
            }

            if (text.front() == '[') {
                if (text.back() != ']') {
                    throw Error(location(file, lineNumber) + "Malformed section header");
                }
                section = lower(trim(text.substr(1, text.size() - 2)));
                continue;
            }

            const std::size_t equals = text.find('=');
            if (equals == std::string_view::npos) {
                throw Error(location(file, lineNumber) + "Expected name=expression");
            }

            const std::string name(trim(text.substr(0, equals)));
            const std::string value(unquote(trim(text.substr(equals + 1))));
            if (name.empty() || value.empty()) {
                throw Error(location(file, lineNumber) + "Empty name or expression");
            }

            try {
                if (section.empty() || section == "program" || section == "main") {
                    const std::string key = lower(name);
                    if (key == "formula" || key == "expression" || key == "result") {
                        setMainExpression(value);
                    } else if (section.empty()) {
                        // Convenient top-level definitions are allowed too.
                        setExpression(name, value);
                    } else {
                        throw Error("Unknown program key '" + name + "'");
                    }
                } else if (section == "values") {
                    setValue(name, parseNumber(value));
                } else if (section == "variables" ||
                           section == "expressions" ||
                           section == "formulas") {
                    setExpression(name, value);
                } else {
                    throw Error("Unknown configuration section [" + section + "]");
                }
            } catch (const std::exception& error) {
                throw Error(location(file, lineNumber) + error.what());
            }
        }

        if (!main_) {
            throw Error("Configuration did not define [program] formula=...");
        }
    }

    // Command-line syntax. Configuration files are loaded first; all other
    // arguments are then applied in argument order, so CLI assignments override config.
    //
    //   --config FILE
    //   --formula EXPRESSION
    //   --set NAME=EXPRESSION
    //   --value NAME=NUMBER
    //   NAME=EXPRESSION                 (shorthand for --set)
    void applyCommandLine(int argc, const char* const argv[]) {
        std::vector<std::filesystem::path> configFiles;
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--config" || argument == "-c") {
                requireNext(i, argc, argument);
                configFiles.emplace_back(argv[++i]);
            } else if (startsWith(argument, "--config=")) {
                configFiles.emplace_back(std::string(argument.substr(9)));
            }
        }
        for (const auto& file : configFiles) {
            loadFromConfig(file, false);
        }

        for (int i = 1; i < argc; ++i) {
            const std::string_view argument(argv[i]);
            if (argument == "--config" || argument == "-c") {
                ++i;
                continue;
            }
            if (startsWith(argument, "--config=")) {
                continue;
            }
            if (argument == "--formula" || argument == "-f") {
                requireNext(i, argc, argument);
                setMainExpression(argv[++i]);
                continue;
            }
            if (startsWith(argument, "--formula=")) {
                setMainExpression(std::string(argument.substr(10)));
                continue;
            }
            if (argument == "--set" || argument == "-s") {
                requireNext(i, argc, argument);
                setAssignment(argv[++i]);
                continue;
            }
            if (startsWith(argument, "--set=")) {
                setAssignment(argument.substr(6));
                continue;
            }
            if (argument == "--value" || argument == "-v") {
                requireNext(i, argc, argument);
                applyLiteralAssignment(argv[++i]);
                continue;
            }
            if (startsWith(argument, "--value=")) {
                applyLiteralAssignment(argument.substr(8));
                continue;
            }
            if (argument == "--help" || argument == "-h") {
                continue;
            }
            if (!argument.empty() && argument.front() == '-') {
                throw Error("Unknown command-line option: " + std::string(argument));
            }
            if (argument.find('=') != std::string_view::npos) {
                setAssignment(argument);
                continue;
            }
            throw Error("Unexpected command-line argument: " + std::string(argument));
        }
    }

    [[nodiscard]] double evaluate() const {
        if (!main_) {
            throw Error("No main formula has been configured");
        }
        EvaluationContext context;
        return evaluateCompiled(*main_, context, "<main>");
    }

    [[nodiscard]] double evaluateName(std::string_view name) const {
        EvaluationContext context;
        return resolve(name, context);
    }

    [[nodiscard]] double evaluateExpression(std::string expression) const {
        const CompiledExpression temporary = compileExpression(std::move(expression));
        EvaluationContext context;
        return evaluateCompiled(temporary, context, "<temporary>");
    }

    [[nodiscard]] std::vector<std::string> names() const {
        std::vector<std::string> result;
        result.reserve(values_.size() + expressions_.size());
        for (const auto& [name, _] : values_) {
            result.push_back(name);
        }
        for (const auto& [name, _] : expressions_) {
            result.push_back(name);
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    [[nodiscard]] const std::string& mainSource() const {
        if (!main_) {
            throw Error("No main formula has been configured");
        }
        return main_->source;
    }

private:
    struct CompiledExpression {
        std::string source;
        MathFormula formula;
    };

    struct FunctionRegistration {
        std::string name;
        std::size_t minimumArguments{};
        std::size_t maximumArguments{};
        NativeFunction callback;
    };

    struct EvaluationContext {
        std::unordered_map<std::string, double> memo;
        std::unordered_set<std::string> active;
        std::vector<std::string> chain;
    };

    [[nodiscard]] CompiledExpression compileExpression(std::string expression) const {
        CompiledExpression compiled;
        compiled.source = std::move(expression);
        applyRegistrations(compiled.formula);
        compiled.formula.compile(compiled.source);
        return compiled;
    }

    void applyRegistrations(MathFormula& formula) const {
        for (const auto& [name, value] : customConstants_) {
            formula.registerConstant(name, value);
        }
        for (const auto& [_, function] : customFunctions_) {
            formula.registerFunction(
                function.name,
                function.minimumArguments,
                function.maximumArguments,
                function.callback);
        }
    }

    static void applyFunctionRegistration(
        std::optional<CompiledExpression>& expression,
        const FunctionRegistration& registration) {
        if (!expression) {
            return;
        }
        applyFunctionRegistration(*expression, registration);
    }

    static void applyFunctionRegistration(
        CompiledExpression& expression,
        const FunctionRegistration& registration) {
        expression.formula.registerFunction(
            registration.name,
            registration.minimumArguments,
            registration.maximumArguments,
            registration.callback);
        expression.formula.compile(expression.source);
    }

    [[nodiscard]] double evaluateCompiled(
        const CompiledExpression& expression,
        EvaluationContext& context,
        std::string_view label) const {

        std::vector<double> variables;
        variables.reserve(expression.formula.variables().size());
        for (const std::string& dependency : expression.formula.variables()) {
            try {
                variables.push_back(resolve(dependency, context));
            } catch (const Error&) {
                throw;
            } catch (const std::exception& error) {
                throw Error(
                    "While evaluating '" + std::string(label) + "': " + error.what());
            }
        }

        try {
            return expression.formula.evaluate(variables);
        } catch (const std::exception& error) {
            throw Error("While evaluating '" + std::string(label) + "': " + error.what());
        }
    }

    [[nodiscard]] double resolve(std::string_view requestedName, EvaluationContext& context) const {
        const std::string name(requestedName);

        if (const auto memoized = context.memo.find(name); memoized != context.memo.end()) {
            return memoized->second;
        }

        if (const auto literal = values_.find(name); literal != values_.end()) {
            context.memo[name] = literal->second;
            return literal->second;
        }

        if (const auto expression = expressions_.find(name); expression != expressions_.end()) {
            if (context.active.contains(name)) {
                std::ostringstream message;
                message << "Circular formula dependency: ";
                const auto first = std::find(context.chain.begin(), context.chain.end(), name);
                for (auto it = first; it != context.chain.end(); ++it) {
                    if (it != first) {
                        message << " -> ";
                    }
                    message << *it;
                }
                message << " -> " << name;
                throw Error(message.str());
            }

            context.active.insert(name);
            context.chain.push_back(name);
            try {
                const double result = evaluateCompiled(expression->second, context, name);
                context.chain.pop_back();
                context.active.erase(name);
                context.memo[name] = result;
                return result;
            } catch (...) {
                context.chain.pop_back();
                context.active.erase(name);
                throw;
            }
        }

        if (resolver_) {
            if (const std::optional<double> resolved = resolver_(name)) {
                context.memo[name] = *resolved;
                return *resolved;
            }
        }

        std::ostringstream message;
        message << "Missing variable '" << name << "'";
        if (!context.chain.empty()) {
            message << " required by ";
            for (std::size_t i = 0; i < context.chain.size(); ++i) {
                if (i != 0) {
                    message << " -> ";
                }
                message << context.chain[i];
            }
        }
        throw Error(message.str());
    }

    void applyLiteralAssignment(std::string_view assignment) {
        const auto [name, value] = splitAssignment(assignment);
        setValue(name, parseNumber(value));
    }

    static std::pair<std::string, std::string> splitAssignment(std::string_view assignment) {
        const std::size_t equals = assignment.find('=');
        if (equals == std::string_view::npos) {
            throw Error("Expected NAME=EXPRESSION, received: " + std::string(assignment));
        }
        const std::string name(trim(assignment.substr(0, equals)));
        const std::string expression(unquote(trim(assignment.substr(equals + 1))));
        if (name.empty() || expression.empty()) {
            throw Error("Empty name or expression in assignment: " + std::string(assignment));
        }
        return {name, expression};
    }

    static double parseNumber(std::string_view text) {
        const std::string value(trim(text));
        std::size_t consumed = 0;
        try {
            const double result = std::stod(value, &consumed);
            if (consumed != value.size()) {
                throw Error("Expected a numeric literal, received: " + value);
            }
            return result;
        } catch (const Error&) {
            throw;
        } catch (const std::exception&) {
            throw Error("Expected a numeric literal, received: " + value);
        }
    }

    static void requireNext(int index, int argc, std::string_view option) {
        if (index + 1 >= argc) {
            throw Error("Missing value after " + std::string(option));
        }
    }

    static bool startsWith(std::string_view value, std::string_view prefix) {
        return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
    }

    static std::string location(const std::filesystem::path& file, std::size_t line) {
        return file.string() + ":" + std::to_string(line) + ": ";
    }

    static std::string lower(std::string_view value) {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return result;
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

    static std::string_view unquote(std::string_view text) {
        if (text.size() >= 2 &&
            ((text.front() == '"' && text.back() == '"') ||
             (text.front() == '\'' && text.back() == '\''))) {
            text.remove_prefix(1);
            text.remove_suffix(1);
        }
        return text;
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

    std::optional<CompiledExpression> main_;
    std::unordered_map<std::string, double> values_;
    std::unordered_map<std::string, CompiledExpression> expressions_;
    ValueResolver resolver_;

    std::unordered_map<std::string, double> customConstants_;
    std::unordered_map<std::string, FunctionRegistration> customFunctions_;
};
