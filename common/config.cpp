#include "config.h"

#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace pt = boost::property_tree;

namespace {

std::string trim(const std::string& s) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    auto begin = std::find_if_not(s.begin(), s.end(), isSpace);
    auto end = std::find_if_not(s.rbegin(), s.rend(), isSpace).base();
    return begin < end ? std::string(begin, end) : std::string();
}

// Убирает пробелы и обрамляющие кавычки: "C:\Work\Documents" -> C:\Work\Documents
std::string unquote(const std::string& raw) {
    std::string s = trim(raw);
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

std::string getRequired(const pt::ptree& tree, const std::string& key) {
    const auto value = tree.get_optional<std::string>(key);
    if (!value) {
        throw std::runtime_error("В файле конфигурации отсутствует параметр '" + key + "'");
    }
    std::string result = unquote(*value);
    if (result.empty()) {
        throw std::runtime_error("Параметр '" + key + "' в файле конфигурации пуст");
    }
    return result;
}

std::string getOptional(const pt::ptree& tree, const std::string& key, const std::string& def) {
    const auto value = tree.get_optional<std::string>(key);
    return value ? unquote(*value) : def;
}

unsigned long parseNumber(const std::string& key, const std::string& text, unsigned long maxValue) {
    std::size_t pos = 0;
    unsigned long value = 0;
    try {
        if (!text.empty() && text.front() == '-') {
            throw std::invalid_argument("negative");
        }
        value = std::stoul(text, &pos);
    } catch (const std::exception&) {
        throw std::runtime_error("Параметр '" + key + "' должен быть неотрицательным числом, получено: '" + text + "'");
    }
    if (pos != text.size() || value > maxValue) {
        throw std::runtime_error("Некорректное значение параметра '" + key + "': '" + text + "'");
    }
    return value;
}

std::vector<std::string> parseExtensions(const std::string& list) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t comma = std::min(list.find(',', start), list.size());
        std::string ext = trim(list.substr(start, comma - start));
        if (!ext.empty() && ext.front() == '.') {
            ext.erase(0, 1);
        }
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!ext.empty() && std::find(result.begin(), result.end(), ext) == result.end()) {
            result.push_back(ext);
        }
        start = comma + 1;
    }
    if (result.empty()) {
        throw std::runtime_error("Список расширений файлов (indexer.extensions) пуст");
    }
    return result;
}

// Экранирование значения для строки подключения libpq: 'значение с \' и \\'
std::string quoteConnValue(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    out += '\'';
    return out;
}

}  // namespace

std::string DatabaseConfig::connectionString() const {
    return "host=" + quoteConnValue(host) +
           " port=" + std::to_string(port) +
           " dbname=" + quoteConnValue(dbname) +
           " user=" + quoteConnValue(user) +
           " password=" + quoteConnValue(password);
}

Config Config::load(const std::string& path) {
    pt::ptree tree;
    try {
        pt::read_ini(path, tree);
    } catch (const pt::ini_parser_error& e) {
        throw std::runtime_error("Не удалось прочитать файл конфигурации '" + path + "': " + e.message());
    }

    Config cfg;

    cfg.database.host = getRequired(tree, "database.host");
    cfg.database.port = static_cast<unsigned>(parseNumber("database.port", getRequired(tree, "database.port"), 65535));
    cfg.database.dbname = getRequired(tree, "database.dbname");
    cfg.database.user = getRequired(tree, "database.user");
    cfg.database.password = getOptional(tree, "database.password", "");

    cfg.indexer.startDirectory = getRequired(tree, "indexer.start_directory");
    cfg.indexer.extensions = parseExtensions(getRequired(tree, "indexer.extensions"));
    cfg.indexer.threads = parseNumber("indexer.threads", getOptional(tree, "indexer.threads", "0"), 1024);

    cfg.search.maxResults = parseNumber("search.max_results", getOptional(tree, "search.max_results", "10"), 1000);
    cfg.search.maxQueryWords = parseNumber("search.max_query_words", getOptional(tree, "search.max_query_words", "4"), 100);
    if (cfg.search.maxResults == 0 || cfg.search.maxQueryWords == 0) {
        throw std::runtime_error("Параметры search.max_results и search.max_query_words должны быть больше нуля");
    }

    return cfg;
}
