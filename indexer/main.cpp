// Программа «Индексатор»
//
// Использование:
//   indexer [путь_к_config.ini] [каталог]
//
// По умолчанию конфигурация читается из config.ini в текущем каталоге.
// Если указан каталог, он используется вместо indexer.start_directory из конфигурации.

#include "config.h"
#include "console.h"
#include "indexer.h"

#include <pqxx/pqxx>

#include <chrono>
#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    setupConsole();

    const std::string configPath = argc > 1 ? argv[1] : "config.ini";

    try {
        Config config = Config::load(configPath);
        if (argc > 2) {
            config.indexer.startDirectory = argv[2];
        }

        std::cout << "Индексация каталога: " << config.indexer.startDirectory << '\n';
        const auto started = std::chrono::steady_clock::now();

        Indexer indexer(config.database, config.indexer);
        const IndexingStats stats = indexer.run();

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);

        std::cout << "\nИндексация завершена за " << elapsed.count() / 1000.0 << " с.\n"
                  << "  найдено файлов:            " << stats.found << '\n'
                  << "  проиндексировано:          " << stats.indexed << '\n'
                  << "  с ошибками:                " << stats.failed << '\n'
                  << "  удалено устаревших из БД:  " << stats.removed << '\n';

        return stats.failed == 0 ? 0 : 2;
    } catch (const pqxx::broken_connection& e) {
        std::cerr << "Ошибка подключения к базе данных: " << e.what() << '\n';
    } catch (const pqxx::sql_error& e) {
        std::cerr << "Ошибка базы данных: " << e.what() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << '\n';
    } catch (...) {
        std::cerr << "Внутренняя ошибка программы\n";
    }
    return 1;
}
