#pragma once

#include "config.h"
#include "database.h"
#include "text_processor.h"

#include <pqxx/pqxx>

#include <memory>
#include <string>
#include <vector>

struct SearchResponse {
    std::vector<std::string> words;          // слова, по которым выполнен поиск
    std::vector<std::string> ignoredWords;   // слова сверх лимита длины запроса
    std::vector<db::SearchResult> results;
};

// Программа-поисковик: разбирает запрос так же, как индексатор разбирает
// документы, и ищет документы, содержащие все слова запроса.
class SearchEngine {
public:
    SearchEngine(DatabaseConfig dbConfig, SearchConfig searchConfig);

    SearchResponse search(const std::string& query);

private:
    pqxx::connection& connection();

    DatabaseConfig dbConfig_;
    SearchConfig config_;
    TextProcessor textProcessor_;
    std::unique_ptr<pqxx::connection> connection_;
};
