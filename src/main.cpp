#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <future>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

// ==========================================
// Модели данных и структуры
// ==========================================

struct ApiParam {
    std::string name;
    std::string type;
};

struct ApiMethod {
    std::string httpMethod; // GET, POST, PUT, DELETE
    std::string route;
    std::string returnType;
    std::string bodyType;
    std::vector<ApiParam> params;
    bool isAsync = false;
    bool hasMiddleware = false;
    bool hasFilter = false;
    
    // Анализ кода функции
    bool hasExceptionHandling = false;
    bool hasValidation = false;
    bool hasCaching = false;
    
    std::vector<std::string> issues;
};

struct ApiController {
    std::string name;
    std::string baseRoute;
    std::vector<ApiMethod> methods;
};

struct ApiAnalysisResult {
    std::map<std::string, ApiController> controllers;
    std::set<std::string> dataModels; // DTO, ViewModel
    std::vector<std::string> generalRecommendations;
};

// ==========================================
// Логирование (api_analysis_log.txt)
// ==========================================

class Logger {
    std::ofstream logFile;
    std::mutex logMutex;
public:
    Logger(const std::string& filename = "api_analysis_log.txt") {
        logFile.open(filename, std::ios::app);
    }

    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(logMutex);
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        if (logFile.is_open()) {
            logFile << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] "
                    << message << std::endl;
        }
    }
};

Logger g_logger;

// ==========================================
// Вспомогательные функции
// ==========================================

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// ==========================================
// Парсер исходного кода
// ==========================================

class ApiParser {
public:
    static void parseFile(const fs::path& filePath, ApiAnalysisResult& result, std::mutex& resultMutex) {
        std::ifstream file(filePath);
        if (!file.is_open()) return;

        std::string line;
        ApiController currentController;
        bool inController = false;

        ApiMethod currentMethod;
        bool buildingMethod = false;

        std::string methodBodyAccumulator;
        bool insideFunctionBody = false;
        int braceDepth = 0;

        auto finalizeMethod = [&](ApiMethod& method, const std::string& body) {
            // Анализ содержимого метода
            if (body.find("try") != std::string::npos && body.find("catch") != std::string::npos) {
                method.hasExceptionHandling = true;
            }
            if (body.find("if") != std::string::npos && 
               (body.find("throw") != std::string::npos || body.find("invalid_argument") != std::string::npos || body.find("empty()") != std::string::npos)) {
                method.hasValidation = true;
            }
            if (body.find("cache") != std::string::npos || body.find("redis") != std::string::npos || body.find("memcached") != std::string::npos) {
                method.hasCaching = true;
            }

            // Выявление проблем
            if (!method.hasValidation && !method.params.empty()) {
                method.issues.push_back("Отсутствует валидация параметров.");
            }
            if (!method.hasExceptionHandling) {
                method.issues.push_back("Отсутствует обработка исключений.");
            }
            if (method.httpMethod == "GET" && !method.hasCaching) {
                method.issues.push_back("Отсутствует кэширование.");
            }
            if (method.httpMethod == "GET" && (body.find("save") != std::string::npos || body.find("delete") != std::string::npos || body.find("update") != std::string::npos)) {
                method.issues.push_back("Потенциально неправильное использование HTTP-метода GET для изменения данных.");
            }
        };

        while (std::getline(file, line)) {
            std::string trimmed = trim(line);

            if (insideFunctionBody) {
                methodBodyAccumulator += line + "\n";
                for (char c : line) {
                    if (c == '{') braceDepth++;
                    else if (c == '}') braceDepth--;
                }
                if (braceDepth <= 0) {
                    insideFunctionBody = false;
                    finalizeMethod(currentMethod, methodBodyAccumulator);
                    
                    if (inController) {
                        currentController.methods.push_back(currentMethod);
                    }
                    currentMethod = ApiMethod();
                    buildingMethod = false;
                    methodBodyAccumulator.clear();
                }
                continue;
            }

            if (trimmed.rfind("//", 0) == 0) {
                std::string commentContent = trim(trimmed.substr(2));

                if (commentContent.rfind("@Controller", 0) == 0) {
                    if (inController && !currentController.name.empty()) {
                        std::lock_guard<std::mutex> lock(resultMutex);
                        result.controllers[currentController.name] = currentController;
                    }
                    currentController = ApiController();
                    currentController.name = trim(commentContent.substr(11));
                    inController = true;
                } 
                else if (commentContent.rfind("@Route", 0) == 0) {
                    std::string routeVal = trim(commentContent.substr(6));
                    if (buildingMethod) {
                        currentMethod.route = routeVal;
                    } else if (inController) {
                        currentController.baseRoute = routeVal;
                    }
                } 
                else if (commentContent.rfind("@GET", 0) == 0) {
                    buildingMethod = true;
                    currentMethod.httpMethod = "GET";
                    currentMethod.route = trim(commentContent.substr(4));
                } 
                else if (commentContent.rfind("@POST", 0) == 0) {
                    buildingMethod = true;
                    currentMethod.httpMethod = "POST";
                    currentMethod.route = trim(commentContent.substr(5));
                } 
                else if (commentContent.rfind("@PUT", 0) == 0) {
                    buildingMethod = true;
                    currentMethod.httpMethod = "PUT";
                    currentMethod.route = trim(commentContent.substr(4));
                } 
                else if (commentContent.rfind("@DELETE", 0) == 0) {
                    buildingMethod = true;
                    currentMethod.httpMethod = "DELETE";
                    currentMethod.route = trim(commentContent.substr(7));
                } 
                else if (commentContent.rfind("@Param", 0) == 0) {
                    std::istringstream iss(commentContent.substr(6));
                    std::string pName, pType;
                    iss >> pName >> pType;
                    if (!pName.empty()) {
                        currentMethod.params.push_back({pName, pType});
                    }
                } 
                else if (commentContent.rfind("@Body", 0) == 0) {
                    currentMethod.bodyType = trim(commentContent.substr(5));
                    if (!currentMethod.bodyType.empty()) {
                        std::lock_guard<std::mutex> lock(resultMutex);
                        result.dataModels.insert(currentMethod.bodyType);
                    }
                } 
                else if (commentContent.rfind("@Return", 0) == 0) {
                    currentMethod.returnType = trim(commentContent.substr(7));
                    if (!currentMethod.returnType.empty()) {
                        std::lock_guard<std::mutex> lock(resultMutex);
                        result.dataModels.insert(currentMethod.returnType);
                    }
                }
                else if (commentContent.rfind("@Async", 0) == 0) {
                    currentMethod.isAsync = true;
                }
                else if (commentContent.rfind("@Middleware", 0) == 0) {
                    currentMethod.hasMiddleware = true;
                }
                else if (commentContent.rfind("@Filter", 0) == 0) {
                    currentMethod.hasFilter = true;
                }
            } 
            else if (buildingMethod && !trimmed.empty() && trimmed.rfind("//", 0) != 0) {
                // Вход в реализацию метода
                if (trimmed.find("{") != std::string::npos) {
                    insideFunctionBody = true;
                    braceDepth = 0;
                    for (char c : trimmed) {
                        if (c == '{') braceDepth++;
                        else if (c == '}') braceDepth--;
                    }
                    methodBodyAccumulator = trimmed + "\n";
                }
            }
        }

        if (inController && !currentController.name.empty()) {
            std::lock_guard<std::mutex> lock(resultMutex);
            result.controllers[currentController.name] = currentController;
        }
    }
};

// ==========================================
// Параллельный обход каталога
// ==========================================

ApiAnalysisResult analyzeDirectory(const fs::path& sourcePath) {
    ApiAnalysisResult result;
    std::mutex resultMutex;
    std::vector<std::future<void>> futures;

    if (!fs::exists(sourcePath) || !fs::is_directory(sourcePath)) {
        std::cerr << "Ошибка: Путь к исходному коду не существует или не является директорией: " << sourcePath << std::endl;
        g_logger.log("ERROR: Directory not found: " + sourcePath.string());
        return result;
    }

    g_logger.log("Начало сканирования директории: " + sourcePath.string());

    for (const auto& entry : fs::recursive_directory_iterator(sourcePath)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".cc") {
                futures.push_back(std::async(std::launch::async, ApiParser::parseFile, entry.path(), std::ref(result), std::ref(resultMutex)));
            }
        }
    }

    for (auto& f : futures) {
        f.wait();
    }

    g_logger.log("Сканирование завершено. Найдено контроллеров: " + std::to_string(result.controllers.size()));
    return result;
}

// ==========================================
// Генерация документации (Markdown, HTML, JSON)
// ==========================================

class DocumentationGenerator {
public:
    static std::string generateMarkdown(const ApiAnalysisResult& result) {
        std::stringstream ss;
        ss << "# Документация API\n\n";

        for (const auto& [name, ctrl] : result.controllers) {
            ss << "## Контроллер: " << name << "\n";
            if (!ctrl.baseRoute.empty()) {
                ss << "**Базовый маршрут:** `" << ctrl.baseRoute << "`\n\n";
            }
            
            for (const auto& m : ctrl.methods) {
                ss << "### " << m.httpMethod << " " << m.route << "\n";
                if (!m.params.empty()) {
                    ss << "**Параметры:**\n";
                    for (const auto& p : m.params) {
                        ss << "- `" << p.name << "` (" << p.type << ")\n";
                    }
                }
                if (!m.bodyType.empty()) ss << "**Тело запроса:** `" << m.bodyType << "`\n";
                if (!m.returnType.empty()) ss << "**Возвращает:** `" << m.returnType << "`\n";
                ss << "\n";
            }
        }

        if (!result.dataModels.empty()) {
            ss << "## Модели данных (DTO / ViewModel)\n";
            for (const auto& model : result.dataModels) {
                ss << "- `" << model << "`\n";
            }
        }

        return ss.str();
    }

    static std::string generateHTML(const ApiAnalysisResult& result) {
        std::stringstream ss;
        ss << "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>API Documentation</title>";
        ss << "<style>body{font-family:sans-serif;margin:20px;} h2{color:#2c3e50;} .method{background:#f8f9fa;padding:10px;margin-bottom:10px;border-left:4px solid #3498db;}</style>";
        ss << "</head><body>\n<h1>Документация API</h1>\n";

        for (const auto& [name, ctrl] : result.controllers) {
            ss << "<h2>Контроллер: " << name << "</h2>\n";
            for (const auto& m : ctrl.methods) {
                ss << "<div class=\"method\">\n";
                ss << "<h3>" << m.httpMethod << " " << m.route << "</h3>\n";
                if (!m.params.empty()) {
                    ss << "<p><b>Параметры:</b></p><ul>\n";
                    for (const auto& p : m.params) {
                        ss << "<li>" << p.name << " (" << p.type << ")</li>\n";
                    }
                    ss << "</ul>\n";
                }
                if (!m.bodyType.empty()) ss << "<p><b>Тело запроса:</b> " << m.bodyType << "</p>\n";
                if (!m.returnType.empty()) ss << "<p><b>Возвращает:</b> " << m.returnType << "</p>\n";
                ss << "</div>\n";
            }
        }
        ss << "</body></html>\n";
        return ss.str();
    }

    static std::string generateJSON(const ApiAnalysisResult& result) {
        std::stringstream ss;
        ss << "{\n  \"controllers\": [\n";
        bool firstCtrl = true;
        for (const auto& [name, ctrl] : result.controllers) {
            if (!firstCtrl) ss << ",\n";
            firstCtrl = false;
            ss << "    {\n      \"name\": \"" << name << "\",\n";
            ss << "      \"baseRoute\": \"" << ctrl.baseRoute << "\",\n";
            ss << "      \"methods\": [\n";
            bool firstM = true;
            for (const auto& m : ctrl.methods) {
                if (!firstM) ss << ",\n";
                firstM = false;
                ss << "        {\n";
                ss << "          \"httpMethod\": \"" << m.httpMethod << "\",\n";
                ss << "          \"route\": \"" << m.route << "\",\n";
                ss << "          \"returnType\": \"" << m.returnType << "\"\n";
                ss << "        }";
            }
            ss << "\n      ]\n    }";
        }
        ss << "\n  ]\n}\n";
        return ss.str();
    }
};

// ==========================================
// Генерация текстового отчета и рекомендаций
// ==========================================

std::string generateReport(const ApiAnalysisResult& result) {
    std::stringstream ss;
    ss << "=== Отчёт об анализе архитектуры API ===\n\n";

    int recCounter = 1;
    for (const auto& [name, ctrl] : result.controllers) {
        ss << "Контроллер: " << name << "\n";
        for (const auto& m : ctrl.methods) {
            ss << "  Метод " << m.httpMethod << " " << m.route << ":\n";
            if (m.issues.empty()) {
                ss << "    Замечаний не обнаружено.\n";
            } else {
                for (const auto& issue : m.issues) {
                    ss << "    [Проблема] " << issue << "\n";
                }
            }
        }
        ss << "\n";
    }

    ss << "Рекомендации:\n";
    for (const auto& [name, ctrl] : result.controllers) {
        for (const auto& m : ctrl.methods) {
            for (const auto& issue : m.issues) {
                ss << "  " << recCounter++ << ". [" << ctrl.name << " -> " << m.httpMethod << " " << m.route << "] " << issue << "\n";
            }
        }
    }

    return ss.str();
}

// ==========================================
// Вывод в терминал с цветам ANSI
// ==========================================

void printTerminalSummary(const ApiAnalysisResult& result) {
    std::cout << "\033[1;36m=== Анализ API ===\033[0m\n\n";

    for (const auto& [name, ctrl] : result.controllers) {
        std::cout << "\033[1;33mКонтроллер: " << name << "\033[0m\n";
        std::cout << "  Методы:\n";
        for (const auto& m : ctrl.methods) {
            std::cout << "    - \033[1;32m" << m.httpMethod << " " << m.route << "\033[0m\n";
            if (!m.params.empty()) {
                std::cout << "        Параметры: ";
                for (size_t i = 0; i < m.params.size(); ++i) {
                    std::cout << m.params[i].name << " (" << m.params[i].type << ")" << (i + 1 < m.params.size() ? ", " : "");
                }
                std::cout << "\n";
            }
            if (!m.returnType.empty()) {
                std::cout << "        Возвращаемые данные: " << m.returnType << "\n";
            }
            if (!m.issues.empty()) {
                for (const auto& issue : m.issues) {
                    std::cout << "        \033[1;31mПроблемы: " << issue << "\033[0m\n";
                }
            }
        }
        std::cout << "\n";
    }
}

// ==========================================
// Интерактивный режим TUI
// ==========================================

void runInteractiveMode(const ApiAnalysisResult& result) {
    printTerminalSummary(result);

    if (result.controllers.empty()) {
        std::cout << "Контроллеры не найдены.\n";
        return;
    }

    std::vector<std::string> ctrlNames;
    for (const auto& [name, _] : result.controllers) {
        ctrlNames.push_back(name);
    }

    std::cout << "Доступные контроллеры:\n";
    for (size_t i = 0; i < ctrlNames.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << ctrlNames[i] << "\n";
    }

    std::cout << "\nВыберите контроллер (1-" << ctrlNames.size() << "): ";
    int choice = 0;
    std::cin >> choice;

    if (choice < 1 || choice > static_cast<int>(ctrlNames.size())) {
        std::cout << "Неверный выбор.\n";
        return;
    }

    std::string selectedCtrlName = ctrlNames[choice - 1];
    const auto& ctrl = result.controllers.at(selectedCtrlName);

    std::cout << "\nКонтроллер: " << ctrl.name << "\nМетоды:\n";
    for (size_t i = 0; i < ctrl.methods.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << ctrl.methods[i].httpMethod << " " << ctrl.methods[i].route << "\n";
    }

    std::cout << "\nВыберите метод для подробного анализа (1-" << ctrl.methods.size() << "): ";
    int mChoice = 0;
    std::cin >> mChoice;

    if (mChoice >= 1 && mChoice <= static_cast<int>(ctrl.methods.size())) {
        const auto& m = ctrl.methods[mChoice - 1];
        std::cout << "\n\033[1;34m[Подробный анализ метода " << m.httpMethod << " " << m.route << "]\033[0m\n";
        if (m.issues.empty()) {
            std::cout << "Замечаний по архитектуре нет.\n";
        } else {
            for (const auto& issue : m.issues) {
                std::cout << " - " << issue << "\n";
            }
        }
    }

    std::cout << "\nСохранить документацию? (y/n): ";
    char saveChar;
    std::cin >> saveChar;
    if (saveChar == 'y' || saveChar == 'Y') {
        std::cout << "Имя файла: ";
        std::string outFilename;
        std::cin >> outFilename;
        std::ofstream outFile(outFilename);
        if (outFile.is_open()) {
            outFile << DocumentationGenerator::generateMarkdown(result);
            std::cout << "[Сохранено]\n";
            g_logger.log("Документация сохранена в интерактивном режиме: " + outFilename);
        }
    }
}

// ==========================================
// Главная функция CLI
// ==========================================

int main(int argc, char* argv[]) {
    std::string sourcePath = "";
    std::string format = "markdown";
    std::string outputDoc = "";
    std::string reportFile = "";
    bool interactive = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--source" && i + 1 < argc) sourcePath = argv[++i];
        else if (arg == "--format" && i + 1 < argc) format = argv[++i];
        else if (arg == "--output" && i + 1 < argc) outputDoc = argv[++i];
        else if (arg == "--report" && i + 1 < argc) reportFile = argv[++i];
        else if (arg == "--interactive") interactive = true;
    }

    if (sourcePath.empty() && !interactive) {
        std::cout << "Использование:\n";
        std::cout << "  ./api-analyzer --source <путь> [--format markdown|html|json] [--output <файл>] [--report <файл>]\n";
        std::cout << "  ./api-analyzer --interactive\n";
        return 1;
    }

    if (sourcePath.empty() && interactive) {
        sourcePath = ".";
    }

    ApiAnalysisResult result = analyzeDirectory(sourcePath);

    if (!outputDoc.empty()) {
        std::ofstream outFile(outputDoc);
        if (outFile.is_open()) {
            if (format == "html") outFile << DocumentationGenerator::generateHTML(result);
            else if (format == "json") outFile << DocumentationGenerator::generateJSON(result);
            else outFile << DocumentationGenerator::generateMarkdown(result);
            std::cout << "Документация сохранена в файл: " << outputDoc << std::endl;
            g_logger.log("Документация сохранена: " + outputDoc);
        }
    }

    if (!reportFile.empty()) {
        std::ofstream repFile(reportFile);
        if (repFile.is_open()) {
            repFile << generateReport(result);
            std::cout << "Отчёт сохранён в файл: " << reportFile << std::endl;
            g_logger.log("Отчёт сохранён: " + reportFile);
        }
    }

    if (interactive) {
        runInteractiveMode(result);
    } else if (outputDoc.empty() && reportFile.empty()) {
        printTerminalSummary(result);
    }

    return 0;
}