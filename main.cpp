#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include <windows.h>
#include <filesystem>
#include "plugin_interface.h"

namespace fs = std::filesystem;

// ============================================================================
// Менеджер плагинов
// ============================================================================
class PluginManager {
private:
    struct LoadedPlugin {
        HMODULE handle;
        PluginInfo info;
    };

    std::vector<LoadedPlugin> loadedPlugins;
    std::map<std::string, MathFunction> registry;

public:
    // Загрузка всех DLL из папки
    void LoadFromDirectory(const std::string& path) {
        if (!fs::exists(path)) {
            std::cerr << "[Warning] Plugins directory not found: " << path << std::endl;
            return;
        }

        std::cout << "Scanning plugins in " << path << "..." << std::endl;
        int count = 0;

        try {
            for (const auto& entry : fs::directory_iterator(path)) {
                if (entry.path().extension() == ".dll") {
                    try {
                        LoadSinglePlugin(entry.path().string());
                        count++;
                    }
                    catch (const std::exception& e) {
                        std::cerr << "[Error] Failed to load " << entry.path().filename()
                            << ": " << e.what() << std::endl;
                    }
                }
            }
        }
        catch (const fs::filesystem_error& e) {
            std::cerr << "[Error] Filesystem error: " << e.what() << std::endl;
        }

        if (count == 0) {
            std::cout << "No valid plugins loaded." << std::endl;
        }
        else {
            std::cout << "Successfully loaded " << count << " plugin(s)." << std::endl;
        }
    }

    // Загрузка конкретной DLL
    void LoadSinglePlugin(const std::string& dllPath) {
        // 1. Загрузка библиотеки
        HMODULE hLib = LoadLibraryA(dllPath.c_str());
        if (!hLib) {
            throw std::runtime_error("LoadLibrary failed (Error code: " +
                std::to_string(GetLastError()) + ")");
        }

        // 2. Поиск экспорта
        GetPluginInfoFunc getInfo = (GetPluginInfoFunc)GetProcAddress(hLib, PLUGIN_EXPORT_NAME);
        if (!getInfo) {
            FreeLibrary(hLib);
            throw std::runtime_error("Missing export '" + std::string(PLUGIN_EXPORT_NAME) + "'");
        }

        // 3. Получение информации
        PluginInfo info = getInfo();

        // 4. Валидация
        if (!info.functionName || !info.functionPtr) {
            FreeLibrary(hLib);
            throw std::runtime_error("Plugin returned null function or name");
        }

        // 5. Регистрация
        if (registry.count(info.functionName)) {
            FreeLibrary(hLib);
            throw std::runtime_error("Duplicate function name: " + std::string(info.functionName));
        }

        registry[info.functionName] = info.functionPtr;
        loadedPlugins.push_back({ hLib, info });
        std::cout << "  -> Registered: " << info.functionName << "()" << std::endl;
    }

    // Получение функции по имени
    MathFunction GetFunction(const std::string& name) {
        auto it = registry.find(name);
        return (it != registry.end()) ? it->second : nullptr;
    }

    // Очистка памяти
    ~PluginManager() {
        for (auto& p : loadedPlugins) {
            FreeLibrary(p.handle);
        }
    }
};

// ============================================================================
// Парсер и Вычислитель выражений
// ============================================================================
class Calculator {
private:
    PluginManager& pluginMgr;
    std::string expr;
    size_t pos;

    void skipWhitespace() {
        while (pos < expr.length() && isspace(expr[pos])) pos++;
    }

    double parseNumber() {
        skipWhitespace();
        size_t start = pos;
        bool hasDot = false;

        // Обработка унарного минуса, если он стоит перед числом
        // В данной реализации унарный минус обрабатывается в parsePrimary для простоты

        while (pos < expr.length() && (isdigit(expr[pos]) || expr[pos] == '.')) {
            if (expr[pos] == '.') {
                if (hasDot) break;
                hasDot = true;
            }
            pos++;
        }

        if (start == pos) throw std::runtime_error("Expected number");
        return std::stod(expr.substr(start, pos - start));
    }

    double parsePrimary() {
        skipWhitespace();

        // 1. Проверка на функцию: имя(
        if (isalpha(expr[pos])) {
            size_t start = pos;
            while (pos < expr.length() && isalnum(expr[pos])) pos++;
            std::string funcName = expr.substr(start, pos - start);

            skipWhitespace();
            if (pos >= expr.length() || expr[pos] != '(') {
                throw std::runtime_error("Expected '(' after function " + funcName);
            }
            pos++; // пропуск '('

            double arg = parseExpression(); // Рекурсивный вызов для аргумента

            skipWhitespace();
            if (pos >= expr.length() || expr[pos] != ')') {
                throw std::runtime_error("Expected ')'");
            }
            pos++; // пропуск ')'

            MathFunction func = pluginMgr.GetFunction(funcName);
            if (!func) {
                throw std::runtime_error("Unknown function: " + funcName);
            }

            // Вызов функции с перехватом исключений
            try {
                return func(arg);
            }
            catch (const std::exception& e) {
                throw std::runtime_error("Function '" + funcName + "' error: " + std::string(e.what()));
            }
            catch (...) {
                throw std::runtime_error("Function '" + funcName + "' threw unknown exception");
            }
        }

        // 2. Проверка на унарный минус
        if (expr[pos] == '-') {
            // Проверяем контекст, чтобы не спутать с вычитанием. 
            // Если это начало или после ( + - * / ^
            bool isUnary = (pos == 0);
            if (!isUnary) {
                char prev = expr[pos - 1];
                if (prev == '(' || prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '^') {
                    isUnary = true;
                }
            }

            if (isUnary) {
                pos++;
                return -parsePrimary();
            }
        }

        // 3. Скобки
        if (expr[pos] == '(') {
            pos++;
            double val = parseExpression();
            skipWhitespace();
            if (pos >= expr.length() || expr[pos] != ')') throw std::runtime_error("Expected ')'");
            pos++;
            return val;
        }

        // 4. Число
        return parseNumber();
    }

    double parsePower() {
        double left = parsePrimary();
        skipWhitespace();
        while (pos < expr.length() && expr[pos] == '^') {
            pos++;
            double right = parsePrimary(); // Возведение в степень правоассоциативно
            left = pow(left, right);
            skipWhitespace();
        }
        return left;
    }

    double parseTerm() {
        double left = parsePower();
        while (true) {
            skipWhitespace();
            if (pos >= expr.length()) break;
            char op = expr[pos];
            if (op != '*' && op != '/') break;
            pos++;
            double right = parsePower();
            if (op == '*') left *= right;
            else {
                if (right == 0.0) throw std::domain_error("Division by zero");
                left /= right;
            }
        }
        return left;
    }

    double parseExpression() {
        double left = parseTerm();
        while (true) {
            skipWhitespace();
            if (pos >= expr.length()) break;
            char op = expr[pos];
            if (op != '+' && op != '-') break;
            pos++;
            double right = parseTerm();
            if (op == '+') left += right;
            else left -= right;
        }
        return left;
    }

public:
    Calculator(PluginManager& pm) : pluginMgr(pm) {}

    double calculate(const std::string& input) {
        if (input.empty()) throw std::runtime_error("Empty input");
        expr = input;
        pos = 0;
        double result = parseExpression();
        if (pos != expr.length()) {
            throw std::runtime_error("Unexpected characters at end of expression");
        }
        return result;
    }
};

// ============================================================================
// Точка входа
// ============================================================================
int main() {
    // Настройка консоли для поддержки UTF-8 (опционально, для кириллицы)
    SetConsoleOutputCP(CP_UTF8);

    PluginManager pm;
    pm.LoadFromDirectory("./plugins");

    Calculator calc(pm);

    std::string input;
    std::cout << "\nCalculator Ready." << std::endl;
    std::cout << "Supported: + - * / ^ () and plugin functions." << std::endl;
    std::cout << "Type 'exit' to quit.\n" << std::endl;

    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, input)) break;

        if (input == "exit" || input == "quit") break;
        if (input.empty()) continue;

        try {
            double result = calc.calculate(input);
            // Вывод с проверкой на целочисленность для красоты
            if (result == (long long)result) {
                std::cout << "= " << (long long)result << std::endl;
            }
            else {
                std::cout << "= " << result << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

    return 0;
}