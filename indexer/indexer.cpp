#include "indexer.h"

#include "database.h"
#include "thread_pool.h"

#include <pqxx/pqxx>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        throw std::runtime_error("не удалось открыть файл");
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        throw std::runtime_error("ошибка чтения файла");
    }
    return content;
}

// Путь в UTF-8 — в таком виде он хранится в базе и выводится пользователю
std::string toUtf8(const fs::path& path) {
    return path.u8string();
}

// Соединения с базой для рабочих потоков: у каждого потока своё соединение,
// так как pqxx::connection нельзя использовать из нескольких потоков одновременно
class ConnectionRegistry {
public:
    explicit ConnectionRegistry(std::string connectionString)
        : connectionString_(std::move(connectionString)) {}

    pqxx::connection& forCurrentThread() {
        const auto id = std::this_thread::get_id();
        pqxx::connection* existing = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = connections_.find(id);
            if (it != connections_.end() && it->second->is_open()) {
                existing = it->second.get();
            }
        }
        if (existing) {
            return *existing;
        }
        // Подключение выполняется вне блокировки, чтобы не задерживать другие потоки
        auto conn = std::make_unique<pqxx::connection>(connectionString_);
        std::lock_guard<std::mutex> lock(mutex_);
        auto& slot = connections_[id];
        slot = std::move(conn);
        return *slot;
    }

private:
    std::string connectionString_;
    std::mutex mutex_;
    std::unordered_map<std::thread::id, std::unique_ptr<pqxx::connection>> connections_;
};

}  // namespace

Indexer::Indexer(DatabaseConfig dbConfig, IndexerConfig indexerConfig)
    : dbConfig_(std::move(dbConfig)), config_(std::move(indexerConfig)) {}

bool Indexer::hasAllowedExtension(const fs::path& file) const {
    std::string ext = file.extension().u8string();
    if (ext.empty()) {
        return false;
    }
    ext.erase(0, 1);  // убираем точку
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return std::find(config_.extensions.begin(), config_.extensions.end(), ext) != config_.extensions.end();
}

std::vector<fs::path> Indexer::collectFiles() const {
    const fs::path root = fs::u8path(config_.startDirectory);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        throw std::runtime_error("Каталог для индексации не найден: " + config_.startDirectory);
    }

    std::vector<fs::path> files;
    // Рекурсивный обход; каталоги без прав доступа пропускаются.
    // Символические ссылки на каталоги не раскрываются, чтобы не уйти в цикл.
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        throw std::runtime_error("Не удалось открыть каталог " + config_.startDirectory + ": " + ec.message());
    }
    for (const fs::recursive_directory_iterator end; it != end; it.increment(ec)) {
        if (ec) {
            std::cerr << "Предупреждение: ошибка обхода каталога: " << ec.message() << '\n';
            ec.clear();
            continue;
        }
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && hasAllowedExtension(it->path())) {
            files.push_back(fs::absolute(it->path(), fileEc).lexically_normal());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

void Indexer::log(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex_);
    std::cout << message << '\n';
}

IndexingStats Indexer::run() {
    indexed_ = 0;
    failed_ = 0;

    // Таблицы создаются один раз до запуска потоков
    {
        pqxx::connection conn(dbConfig_.connectionString());
        db::createSchema(conn);
    }

    const std::vector<fs::path> files = collectFiles();

    std::size_t threadCount = config_.threads;
    if (threadCount == 0) {
        threadCount = std::max(1u, std::thread::hardware_concurrency());
    }
    threadCount = std::max<std::size_t>(1, std::min(threadCount, files.size()));

    log("Найдено файлов: " + std::to_string(files.size()) +
        ". Потоков индексации: " + std::to_string(threadCount));

    ConnectionRegistry connections(dbConfig_.connectionString());
    {
        ThreadPool pool(threadCount);
        for (const auto& file : files) {
            pool.submit([this, &connections, file] {
                try {
                    const auto frequency = textProcessor_.countWords(readFile(file));
                    db::saveDocument(connections.forCurrentThread(), toUtf8(file), frequency);
                    ++indexed_;
                    log("[OK] " + toUtf8(file) + " (уникальных слов: " + std::to_string(frequency.size()) + ")");
                } catch (const std::exception& e) {
                    ++failed_;
                    log("[ОШИБКА] " + toUtf8(file) + ": " + e.what());
                }
            });
        }
        pool.waitAndStop();  // дожидаемся обработки всех файлов
    }  // пул уничтожается раньше соединений, которые он использовал

    // Удаляем из базы документы, которых больше нет в каталоге
    std::vector<std::string> actualPaths;
    actualPaths.reserve(files.size());
    for (const auto& file : files) {
        actualPaths.push_back(toUtf8(file));
    }
    std::size_t removed = 0;
    {
        pqxx::connection conn(dbConfig_.connectionString());
        removed = db::removeStaleDocuments(conn, actualPaths);
    }

    IndexingStats stats;
    stats.found = files.size();
    stats.indexed = indexed_;
    stats.failed = failed_;
    stats.removed = removed;
    return stats;
}
