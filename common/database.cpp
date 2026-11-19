#include "database.h"

#include <algorithm>
#include <utility>

namespace db {

namespace {

constexpr int kMaxTransactionAttempts = 3;

}  // namespace

void createSchema(pqxx::connection& conn) {
    pqxx::work tx(conn);
    tx.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS documents (
            id   SERIAL PRIMARY KEY,
            path TEXT NOT NULL UNIQUE
        )
    )SQL");
    tx.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS words (
            id   SERIAL PRIMARY KEY,
            word VARCHAR(32) NOT NULL UNIQUE
        )
    )SQL");
    tx.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS document_words (
            document_id INTEGER NOT NULL REFERENCES documents (id) ON DELETE CASCADE,
            word_id     INTEGER NOT NULL REFERENCES words (id) ON DELETE CASCADE,
            frequency   INTEGER NOT NULL CHECK (frequency > 0),
            PRIMARY KEY (document_id, word_id)
        )
    )SQL");
    // Поиск идёт от слова к документам — нужен индекс по word_id
    tx.exec("CREATE INDEX IF NOT EXISTS document_words_word_id_idx ON document_words (word_id)");
    tx.commit();
}

void saveDocument(pqxx::connection& conn,
                  const std::string& path,
                  const std::unordered_map<std::string, int>& frequency) {
    // Сортируем слова, чтобы все потоки вставляли их в одном порядке —
    // это исключает взаимные блокировки (deadlock) в PostgreSQL при
    // одновременной вставке одинаковых слов из разных документов
    std::vector<std::pair<std::string, int>> sorted(frequency.begin(), frequency.end());
    std::sort(sorted.begin(), sorted.end());

    std::vector<std::string> words;
    std::vector<int> counts;
    words.reserve(sorted.size());
    counts.reserve(sorted.size());
    for (auto& [word, count] : sorted) {
        words.push_back(std::move(word));
        counts.push_back(count);
    }

    for (int attempt = 1;; ++attempt) {
        try {
            pqxx::work tx(conn);

            const int documentId = tx.exec_params1(
                "INSERT INTO documents (path) VALUES ($1) "
                "ON CONFLICT (path) DO UPDATE SET path = EXCLUDED.path "
                "RETURNING id",
                path)[0].as<int>();

            // Повторная индексация: удаляем старые частоты документа
            tx.exec_params0("DELETE FROM document_words WHERE document_id = $1", documentId);

            if (!words.empty()) {
                // ORDER BY задаёт единый порядок вставки (и блокировок) для всех транзакций
                tx.exec_params0(
                    "INSERT INTO words (word) "
                    "SELECT w FROM unnest($1::text[]) AS w ORDER BY w "
                    "ON CONFLICT (word) DO NOTHING",
                    words);

                tx.exec_params0(
                    "INSERT INTO document_words (document_id, word_id, frequency) "
                    "SELECT $1, w.id, f.cnt "
                    "FROM unnest($2::text[], $3::int[]) AS f (word, cnt) "
                    "JOIN words w ON w.word = f.word",
                    documentId, words, counts);
            }

            tx.commit();
            return;
        } catch (const pqxx::deadlock_detected&) {
            if (attempt >= kMaxTransactionAttempts) throw;
        } catch (const pqxx::serialization_failure&) {
            if (attempt >= kMaxTransactionAttempts) throw;
        }
    }
}

std::size_t removeStaleDocuments(pqxx::connection& conn, const std::vector<std::string>& actualPaths) {
    pqxx::work tx(conn);
    const pqxx::result removed = tx.exec_params(
        "DELETE FROM documents WHERE path <> ALL ($1::text[])", actualPaths);
    tx.exec0(
        "DELETE FROM words w "
        "WHERE NOT EXISTS (SELECT 1 FROM document_words dw WHERE dw.word_id = w.id)");
    tx.commit();
    return static_cast<std::size_t>(removed.affected_rows());
}

std::vector<SearchResult> search(pqxx::connection& conn,
                                 const std::vector<std::string>& words,
                                 std::size_t limit) {
    std::vector<SearchResult> results;
    if (words.empty() || limit == 0) {
        return results;
    }

    // Берём документы, в которых есть ВСЕ слова запроса (HAVING COUNT = число слов;
    // пара документ-слово уникальна благодаря первичному ключу), и сортируем
    // по суммарной частоте этих слов в документе
    pqxx::read_transaction tx(conn);
    const pqxx::result rows = tx.exec_params(
        "SELECT d.path, SUM(dw.frequency)::bigint AS relevance "
        "FROM words w "
        "JOIN document_words dw ON dw.word_id = w.id "
        "JOIN documents d ON d.id = dw.document_id "
        "WHERE w.word = ANY ($1::text[]) "
        "GROUP BY d.id, d.path "
        "HAVING COUNT(*) = $2 "
        "ORDER BY relevance DESC, d.path ASC "
        "LIMIT $3",
        words, static_cast<long long>(words.size()), static_cast<long long>(limit));

    results.reserve(rows.size());
    for (const auto& row : rows) {
        results.push_back({row[0].as<std::string>(), row[1].as<long long>()});
    }
    return results;
}

}  // namespace db
