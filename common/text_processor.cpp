#include "text_processor.h"

#include <boost/locale.hpp>

#include <algorithm>
#include <stdexcept>

namespace bl = boost::locale;

namespace {

constexpr const char* kLocaleName = "ru_RU.UTF-8";

// Выбирает бэкенд Boost.Locale.
// Без ICU: под Windows — "winapi" (перевод в нижний регистр средствами Windows),
// в остальных системах — "posix" или "std" (локаль ru_RU.UTF-8 должна быть установлена).
// С ICU (SEARCH_ENGINE_USE_ICU=ON) используется бэкенд по умолчанию, то есть ICU.
bl::localization_backend_manager makeBackendManager() {
    bl::localization_backend_manager manager = bl::localization_backend_manager::global();
#ifndef SEARCH_ENGINE_USE_ICU
    const std::vector<std::string> available = manager.get_all_backends();
    for (const std::string name : {"winapi", "posix", "std"}) {
        if (std::find(available.begin(), available.end(), name) != available.end()) {
            manager.select(name);
            return manager;
        }
    }
    throw std::runtime_error("В сборке Boost.Locale нет бэкенда без ICU (winapi, posix или std)");
#else
    return manager;
#endif
}

// Символ входит в слово: буквы и цифры любого алфавита.
// Разделителями считаются пробельные и управляющие символы, знаки препинания,
// символы валют и математики, стрелки, рамки, эмодзи и т. п.
bool isWordChar(char32_t c) {
    if (c < 0x80) {
        return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9');
    }
    if (c <= 0xBF) return false;                     // управляющие C1, NBSP, «», §, ©, ° ...
    if (c == 0xD7 || c == 0xF7) return false;        // × ÷
    if (c >= 0x2000 && c <= 0x2BFF) return false;    // пробелы, тире, кавычки, …, №, ™, стрелки, математика, рамки
    if (c >= 0x2E00 && c <= 0x2E7F) return false;    // дополнительная пунктуация
    if (c >= 0x3000 && c <= 0x303F) return false;    // пунктуация CJK
    if (c >= 0xD800 && c <= 0xF8FF) return false;    // суррогаты, область частного использования
    if (c >= 0xFE30 && c <= 0xFE4F) return false;    // формы совместимости CJK
    if (c >= 0xFF00 && c <= 0xFF0F) return false;    // полноширинная пунктуация
    if (c >= 0xFF1A && c <= 0xFF20) return false;
    if (c >= 0xFF3B && c <= 0xFF40) return false;
    if (c >= 0xFF5B && c <= 0xFF65) return false;
    if (c == 0xFEFF || (c >= 0xFFF0 && c <= 0xFFFF)) return false;  // BOM, спецсимволы
    if (c >= 0x1F000 && c <= 0x1FAFF) return false;  // эмодзи и пиктограммы
    return true;
}

}  // namespace

TextProcessor::TextProcessor() {
    bl::generator gen(makeBackendManager());
    locale_ = gen(kLocaleName);
}

std::vector<std::string> TextProcessor::extractWords(const std::string& text) const {
    // Отбрасываем некорректные последовательности UTF-8, чтобы файл в другой
    // кодировке или бинарный мусор не приводили к исключению
    const std::string valid = bl::conv::utf_to_utf<char>(text, bl::conv::skip);
    const std::string lower = bl::to_lower(valid, locale_);
    const std::u32string chars = bl::conv::utf_to_utf<char32_t>(lower, bl::conv::skip);

    std::vector<std::string> words;
    std::u32string current;
    const auto flush = [&] {
        if (current.size() >= kMinWordLength && current.size() <= kMaxWordLength) {
            words.push_back(bl::conv::utf_to_utf<char>(current));
        }
        current.clear();
    };

    for (char32_t c : chars) {
        if (isWordChar(c)) {
            current.push_back(c);
        } else {
            flush();
        }
    }
    flush();
    return words;
}

std::unordered_map<std::string, int> TextProcessor::countWords(const std::string& text) const {
    std::unordered_map<std::string, int> frequency;
    for (auto& word : extractWords(text)) {
        ++frequency[std::move(word)];
    }
    return frequency;
}
