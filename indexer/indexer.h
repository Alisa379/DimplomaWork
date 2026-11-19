#pragma once

#include "config.h"
#include "text_processor.h"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

struct IndexingStats {
    std::size_t found = 0;      // файлов найдено
    std::size_t indexed = 0;    // успешно проиндексировано
    std::size_t failed = 0;     // ошибок при обработке
    std::size_t removed = 0;    // устаревших документов удалено из базы
};

// Программа «Индексатор»: рекурсивно обходит каталог, считает частоту слов
// в подходящих файлах и сохраняет её в базу. Файлы обрабатываются пулом потоков,
// у каждого потока своё соединение с базой.
class Indexer {
public:
    Indexer(DatabaseConfig dbConfig, IndexerConfig indexerConfig);

    IndexingStats run();

private:
    bool hasAllowedExtension(const std::filesystem::path& file) const;
    std::vector<std::filesystem::path> collectFiles() const;
    void log(const std::string& message);

    DatabaseConfig dbConfig_;
    IndexerConfig config_;
    TextProcessor textProcessor_;

    std::atomic<std::size_t> indexed_{0};
    std::atomic<std::size_t> failed_{0};
    std::mutex logMutex_;  // вывод в консоль из разных потоков
};
