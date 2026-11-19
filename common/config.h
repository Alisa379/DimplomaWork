#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Параметры подключения к PostgreSQL
struct DatabaseConfig {
    std::string host;
    unsigned port = 5432;
    std::string dbname;
    std::string user;
    std::string password;

    // Строка подключения в формате libpq ("host=... port=... ...")
    std::string connectionString() const;
};

// Параметры программы «Индексатор»
struct IndexerConfig {
    std::string startDirectory;
    std::vector<std::string> extensions;  // в нижнем регистре, без точки
    std::size_t threads = 0;              // 0 — по числу ядер процессора
};

// Параметры программы-поисковика
struct SearchConfig {
    std::size_t maxResults = 10;
    std::size_t maxQueryWords = 4;
};

struct Config {
    DatabaseConfig database;
    IndexerConfig indexer;
    SearchConfig search;

    // Читает и проверяет ini-файл. Бросает std::runtime_error при ошибках.
    static Config load(const std::string& path);
};
