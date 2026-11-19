#pragma once

#include <pqxx/pqxx>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

// Структура базы данных:
//   documents      (id, path)                       — проиндексированные файлы
//   words          (id, word)                       — словарь
//   document_words (document_id, word_id, frequency) — связь «многие-ко-многим»
//                                                    с частотой слова в документе
namespace db {

struct SearchResult {
    std::string path;
    long long relevance = 0;
};

// Создаёт таблицы и индексы (CREATE ... IF NOT EXISTS)
void createSchema(pqxx::connection& conn);

// Сохраняет документ и частоты его слов в одной транзакции.
// При повторной индексации старые данные документа заменяются новыми.
// Безопасна для одновременного вызова из разных потоков с разными соединениями.
void saveDocument(pqxx::connection& conn,
                  const std::string& path,
                  const std::unordered_map<std::string, int>& frequency);

// Удаляет документы, которых нет в списке актуальных путей (файл удалён или
// больше не подходит под настройки), и слова, не встречающиеся ни в одном документе.
// Возвращает число удалённых документов.
std::size_t removeStaleDocuments(pqxx::connection& conn, const std::vector<std::string>& actualPaths);

// Документы, содержащие все слова запроса, по убыванию суммарной частоты слов
std::vector<SearchResult> search(pqxx::connection& conn,
                                 const std::vector<std::string>& words,
                                 std::size_t limit);

}  // namespace db
