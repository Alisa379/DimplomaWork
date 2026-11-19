#include "search_engine.h"

#include <algorithm>
#include <utility>

SearchEngine::SearchEngine(DatabaseConfig dbConfig, SearchConfig searchConfig)
    : dbConfig_(std::move(dbConfig)), config_(searchConfig) {
    connection();  // проверяем подключение сразу при запуске
}

pqxx::connection& SearchEngine::connection() {
    // Переподключаемся, если соединение было потеряно (например, перезапуск сервера БД)
    if (!connection_ || !connection_->is_open()) {
        connection_.reset();
        connection_ = std::make_unique<pqxx::connection>(dbConfig_.connectionString());
    }
    return *connection_;
}

SearchResponse SearchEngine::search(const std::string& query) {
    SearchResponse response;

    // Уникальные слова запроса в порядке ввода
    for (auto& word : textProcessor_.extractWords(query)) {
        if (std::find(response.words.begin(), response.words.end(), word) != response.words.end() ||
            std::find(response.ignoredWords.begin(), response.ignoredWords.end(), word) != response.ignoredWords.end()) {
            continue;
        }
        if (response.words.size() < config_.maxQueryWords) {
            response.words.push_back(std::move(word));
        } else {
            response.ignoredWords.push_back(std::move(word));
        }
    }

    if (!response.words.empty()) {
        try {
            response.results = db::search(connection(), response.words, config_.maxResults);
        } catch (const pqxx::broken_connection&) {
            connection_.reset();  // при следующем запросе подключимся заново
            throw;
        }
    }
    return response;
}
