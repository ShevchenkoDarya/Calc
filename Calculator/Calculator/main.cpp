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
// Ìåíåäæåð ïëàãèíîâ
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
    // Çàãðóçêà âñåõ DLL èç ïàïêè
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

    // Çàãðóçêà êîíêðåòíîé DLL
    void LoadSinglePlugin(const std::string& dllPath) {
        // 1. Çàãðóçêà áèáëèîòåêè
        HMODULE hLib = LoadLibraryA(dllPath.c_str());
        if (!hLib) {
            throw std::runtime_error("LoadLibrary failed (Error code: " +
                std::to_string(GetLastError()) + ")");
        }

        // 2. Ïîèñê ýêñïîðòà
        GetPluginInfoFunc getInfo = (GetPluginInfoFunc)GetProcAddress(hLib, PLUGIN_EXPORT_NAME);
        if (!getInfo) {
            FreeLibrary(hLib);
            throw std::runtime_error("Missing export '" + std::string(PLUGIN_EXPORT_NAME) + "'");
        }

        // 3. Ïîëó÷åíèå èíôîðìàöèè
        PluginInfo info = getInfo();

        // 4. Âàëèäàöèÿ
        if (!info.functionName || !info.functionPtr) {
            FreeLibrary(hLib);
            throw std::runtime_error("Plugin returned null function or name");
        }

        // 5. Ðåãèñòðàöèÿ
        if (registry.count(info.functionName)) {
            FreeLibrary(hLib);
            throw std::runtime_error("Duplicate function name: " + std::string(info.functionName));
        }

        registry[info.functionName] = info.functionPtr;
        loadedPlugins.push_back({ hLib, info });
        std::cout << "  -> Registered: " << info.functionName << "()" << std::endl;
    }

    // Ïîëó÷åíèå ôóíêöèè ïî èìåíè
    MathFunction GetFunction(const std::string& name) {
        auto it = registry.find(name);
        return (it != registry.end()) ? it->second : nullptr;
    }

    // Î÷èñòêà ïàìÿòè
    ~PluginManager() {
        for (auto& p : loadedPlugins) {
            FreeLibrary(p.handle);
        }
    }
};

// ============================================================================
// Ïàðñåð è Âû÷èñëèòåëü âûðàæåíèé
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

        // Îáðàáîòêà óíàðíîãî ìèíóñà, åñëè îí ñòîèò ïåðåä ÷èñëîì
        // Â äàííîé ðåàëèçàöèè óíàðíûé ìèíóñ îáðàáàòûâàåòñÿ â parsePrimary äëÿ ïðîñòîòû

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

        // 1. Ïðîâåðêà íà ôóíêöèþ: èìÿ(
        if (isalpha(expr[pos])) {
            size_t start = pos;
            while (pos < expr.length() && isalnum(expr[pos])) pos++;
            std::string funcName = expr.substr(start, pos - start);

            skipWhitespace();
            if (pos >= expr.length() || expr[pos] != '(') {
                throw std::runtime_error("Expected '(' after function " + funcName);
            }
            pos++; // ïðîïóñê '('

            double arg = parseExpression(); // Ðåêóðñèâíûé âûçîâ äëÿ àðãóìåíòà

            skipWhitespace();
            if (pos >= expr.length() || expr[pos] != ')') {
                throw std::runtime_error("Expected ')'");
            }
            pos++; // ïðîïóñê ')'

            MathFunction func = pluginMgr.GetFunction(funcName);
            if (!func) {
                throw std::runtime_error("Unknown function: " + funcName);
            }

            // Âûçîâ ôóíêöèè ñ ïåðåõâàòîì èñêëþ÷åíèé
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

        // 2. Ïðîâåðêà íà óíàðíûé ìèíóñ
        if (expr[pos] == '-') {
            // Ïðîâåðÿåì êîíòåêñò, ÷òîáû íå ñïóòàòü ñ âû÷èòàíèåì. 
            // Åñëè ýòî íà÷àëî èëè ïîñëå ( + - * / ^
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

        // 3. Ñêîáêè
        if (expr[pos] == '(') {
            pos++;
            double val = parseExpression();
            skipWhitespace();
            if (pos >= expr.length() || expr[pos] != ')') throw std::runtime_error("Expected ')'");
            pos++;
            return val;
        }

        // 4. ×èñëî
        return parseNumber();
    }

    double parsePower() {
        double left = parsePrimary();
        skipWhitespace();
        while (pos < expr.length() && expr[pos] == '^') {
            pos++;
            double right = parsePrimary(); // Âîçâåäåíèå â ñòåïåíü ïðàâîàññîöèàòèâíî
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
// Òî÷êà âõîäà
// ============================================================================
int main() {
    // Íàñòðîéêà êîíñîëè äëÿ ïîääåðæêè UTF-8 (îïöèîíàëüíî, äëÿ êèðèëëèöû)
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
            // Âûâîä ñ ïðîâåðêîé íà öåëî÷èñëåííîñòü äëÿ êðàñîòû
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
