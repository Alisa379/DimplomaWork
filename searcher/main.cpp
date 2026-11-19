// Программа-поисковик
//
// Использование:
//   searcher [путь_к_config.ini]
//
// Запрос вводится как слова через пробел. Пустая строка, «exit» или «выход»
// (а также конец ввода, Ctrl+D / Ctrl+Z) завершают работу.

#include "config.h"
#include "console.h"
#include "search_engine.h"

#include <pqxx/pqxx>

#include <exception>
#include <iostream>
#include <string>

namespace {

std::string join(const std::vector<std::string>& words) {
    std::string result;
    for (const auto& word : words) {
        if (!result.empty()) result += ' ';
        result += word;
    }
    return result;
}

bool isExitCommand(const std::string& line) {
    return line == "exit" || line == "quit" || line == "выход";
}

void printResponse(const SearchResponse& response, std::size_t maxQueryWords) {
    if (!response.ignoredWords.empty()) {
        std::cout << "Запрос ограничен " << maxQueryWords << " словами, не учтены: "
                  << join(response.ignoredWords) << '\n';
    }
    if (response.words.empty()) {
        std::cout << "В запросе нет слов для поиска (учитываются слова длиной от "
                  << TextProcessor::kMinWordLength << " до " << TextProcessor::kMaxWordLength
                  << " символов).\n";
        return;
    }
    if (response.results.empty()) {
        std::cout << "По запросу «" << join(response.words) << "» ничего не найдено.\n";
        return;
    }
    std::cout << "Результаты по запросу «" << join(response.words) << "»:\n";
    std::size_t position = 1;
    for (const auto& result : response.results) {
        std::cout << "  " << position++ << ". " << result.path
                  << "  (релевантность: " << result.relevance << ")\n";
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    setupConsole();

    const std::string configPath = argc > 1 ? argv[1] : "config.ini";

    std::unique_ptr<SearchEngine> engine;
    Config config;
    try {
        config = Config::load(configPath);
        engine = std::make_unique<SearchEngine>(config.database, config.search);
    } catch (const pqxx::broken_connection& e) {
        std::cerr << "Ошибка подключения к базе данных: " << e.what() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Ошибка запуска: " << e.what() << '\n';
        return 1;
    }

    std::cout << "Поисковая система. Введите запрос (не более " << config.search.maxQueryWords
              << " слов через пробел).\nДля выхода введите пустую строку или «exit».\n";

    std::string line;
    for (;;) {
        std::cout << "\nПоиск> " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << '\n';
            break;  // конец ввода
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find_first_not_of(" \t") == std::string::npos || isExitCommand(line)) {
            break;
        }

        try {
            printResponse(engine->search(line), config.search.maxQueryWords);
        } catch (const std::exception& e) {
            std::cout << "Внутренняя ошибка: " << e.what() << '\n';
        } catch (...) {
            std::cout << "Внутренняя ошибка программы\n";
        }
    }
    return 0;
}
