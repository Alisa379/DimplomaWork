# Поисковая система

Дипломный проект: поиск по текстовым документам в файловой системе на C++17.

Проект состоит из двух программ:

* **indexer** — рекурсивно обходит каталог, очищает текст файлов от знаков препинания, приводит слова к нижнему регистру (Boost.Locale), считает частоту слов и сохраняет её в PostgreSQL. Файлы обрабатываются параллельно пулом потоков.
* **searcher** — консольный поисковик: принимает запрос из слов через пробел, находит документы, содержащие **все** слова запроса, и выводит до 10 из них по убыванию суммарной частоты этих слов.

## Структура проекта

```
CMakeLists.txt
config.ini              — настройки обеих программ
common/                 — общий код
  config.*              — чтение и проверка ini-файла
  text_processor.*      — разбиение текста на слова (Boost.Locale)
  database.*            — схема БД и SQL-запросы (libpqxx)
  thread_pool.*         — пул потоков
  console.*             — UTF-8 в консоли Windows
indexer/                — программа «Индексатор»
searcher/               — программа-поисковик
test_data/              — пример документов для проверки
```

## База данных

```
documents (id, path)                         — проиндексированные файлы
words (id, word)                             — словарь
document_words (document_id, word_id,        — связь «многие-ко-многим»
                frequency)                     с частотой слова в документе
```

Таблицы и индекс создаются автоматически при запуске индексатора (`CREATE TABLE IF NOT EXISTS`).

Запрос поиска:

```sql
SELECT d.path, SUM(dw.frequency) AS relevance
FROM words w
JOIN document_words dw ON dw.word_id = w.id
JOIN documents d ON d.id = dw.document_id
WHERE w.word = ANY ($1)          -- слова запроса
GROUP BY d.id, d.path
HAVING COUNT(*) = $2             -- документ содержит все слова запроса
ORDER BY relevance DESC
LIMIT $3;
```

## Правила обработки текста

* Текст разбивается на слова: словом считается последовательность букв и цифр любого алфавита; знаки препинания, кавычки, тире, табуляция, переводы строк, эмодзи и прочие символы служат разделителями.
* Слова приводятся к нижнему регистру через Boost.Locale (корректно для кириллицы, включая «Ё»).
* Учитываются слова длиной от 3 до 32 символов.
* Файлы читаются как UTF-8; некорректные байты пропускаются, файл не ломает индексацию.
* Запрос в поисковике разбирается по тем же правилам; учитываются первые 4 уникальных слова.

## Boost.Locale и ICU

По умолчанию бэкенд ICU в Boost.Locale **не используется**: в сборке Boost под Windows (vcpkg) ICU требует внешний файл данных `icudt*.dat`, без которого программа завершается с ошибкой `U_MISSING_RESOURCE_ERROR`. Вместо ICU выбирается бэкенд `winapi` (Windows) или `posix`/`std` (Linux, macOS), а разбиение на слова выполняется собственным кодом, поэтому дополнительные файлы не нужны и результат одинаков на всех платформах.

На Linux и macOS для бэкендов `posix`/`std` в системе должна быть установлена локаль `ru_RU.UTF-8` (Ubuntu: `sudo locale-gen ru_RU.UTF-8`). Если её нет, можно собрать проект с ICU: `cmake -S . -B build -DSEARCH_ENGINE_USE_ICU=ON` (на Linux данные ICU входят в системную библиотеку).

## Многопоточность

* Найденные файлы ставятся в очередь пула потоков (`ThreadPool`: очередь под мьютексом + condition_variable). Число потоков задаётся в `config.ini` (`0` — по числу ядер).
* У каждого рабочего потока своё соединение с БД — `pqxx::connection` не разделяется между потоками.
* Каждый документ сохраняется одной транзакцией. Слова вставляются в базу в отсортированном порядке (`ORDER BY`), поэтому параллельные транзакции захватывают блокировки в одном порядке и не попадают в deadlock. На случай редкого конфликта предусмотрен повтор транзакции.
* Счётчики — `std::atomic`, вывод в консоль защищён мьютексом.

Проверено на 3000 файлах в 16 потоков, а также с AddressSanitizer/LeakSanitizer/UBSan и ThreadSanitizer — ошибок, утечек и гонок не обнаружено.

## Повторная индексация

Индексатор можно запускать повторно: данные изменённых файлов перезаписываются, документы, которых больше нет в каталоге, и слова, оставшиеся без документов, удаляются из базы.

## Сборка

Нужны: компилятор с поддержкой C++17, CMake ≥ 3.16, PostgreSQL, libpqxx (7.x), Boost (Locale).

### Linux (Ubuntu / Debian)

```bash
sudo apt install build-essential cmake libpqxx-dev libboost-locale-dev postgresql
sudo locale-gen ru_RU.UTF-8
cmake -S . -B build
cmake --build build -j
```

### Windows (vcpkg)

```powershell
vcpkg install libpqxx boost-locale boost-property-tree --triplet x64-windows
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<путь_к_vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## Настройка

1. Создайте базу данных:
   ```sql
   CREATE DATABASE search_engine;
   ```
2. Укажите параметры подключения, каталог для индексации и расширения файлов в `config.ini`:
   ```ini
   [database]
   host = localhost
   port = 5432
   dbname = search_engine
   user = postgres
   password = postgres

   [indexer]
   start_directory = ./test_data
   extensions = txt,md,ini,csv
   threads = 0

   [search]
   max_results = 10
   max_query_words = 4
   ```

## Запуск

Из корня проекта (относительный `start_directory` считается от текущего каталога):

```bash
./build/indexer                  # конфигурация из ./config.ini
./build/indexer my.ini           # другой файл конфигурации
./build/indexer config.ini /path # другой каталог для индексации

./build/searcher                 # конфигурация из ./config.ini
```

Пример работы поисковика на `test_data`:

```
Поиск> привет мир
Результаты по запросу «привет мир»:
  1. /.../test_data/world.md  (релевантность: 11)
  2. /.../test_data/hello.txt  (релевантность: 8)
  3. /.../test_data/broken.txt  (релевантность: 3)

Поиск> несуществующееслово
По запросу «несуществующееслово» ничего не найдено.
```

Выход из поисковика — пустая строка, `exit` или конец ввода (Ctrl+D / Ctrl+Z). При ошибке базы данных поисковик выводит сообщение о внутренней ошибке и при следующем запросе подключается заново.
