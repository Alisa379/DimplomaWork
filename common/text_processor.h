#pragma once

#include <locale>
#include <string>
#include <unordered_map>
#include <vector>

// Разбивает текст на слова: убирает знаки препинания, табуляцию, переносы строк,
// приводит слова к нижнему регистру (Boost.Locale) и отбрасывает слова
// короче kMinWordLength или длиннее kMaxWordLength символов.
//
// Разбиение на слова выполняется собственным кодом и не зависит от бэкенда
// Boost.Locale, поэтому результат одинаков на всех платформах. Бэкенд ICU
// по умолчанию отключён (см. опцию SEARCH_ENGINE_USE_ICU в CMakeLists.txt):
// в сборке Boost под Windows ICU требует внешний файл данных icudt*.dat.
//
// Объект неизменяем после создания, поэтому безопасен для использования из нескольких потоков.
class TextProcessor {
public:
    static constexpr std::size_t kMinWordLength = 3;
    static constexpr std::size_t kMaxWordLength = 32;

    TextProcessor();

    // Слова текста в порядке появления (с повторами)
    std::vector<std::string> extractWords(const std::string& text) const;

    // Частотный словарь: слово -> количество вхождений
    std::unordered_map<std::string, int> countWords(const std::string& text) const;

private:
    std::locale locale_;
};
