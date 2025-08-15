#pragma once
#include "database.h"
#include <boost/algorithm/string.hpp>
#include <boost/locale.hpp>
#include <regex>
#include <map>

using namespace std;
namespace ba = boost::algorithm;
namespace bl = boost::locale;
using namespace pqxx;

Database::Database(const string& host,
    const string& port,
    const string& dbname,
    const string& user,
    const string& password) :
    conn_("host=" + host +
        " port=" + port +
        " dbname=" + dbname +
        " user=" + user +
        " password=" + password)
{
    if (!conn_.is_open()) {
        throw runtime_error("Failed to connect to database");
    }
    initializeSchema();
}

void Database::initializeSchema() {
    work txn(conn_);

    // Основные таблицы
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS documents (
            id SERIAL PRIMARY KEY,
            url TEXT UNIQUE NOT NULL,
            title TEXT,
            content TEXT,
            last_crawled TIMESTAMP
        )
    )");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS words (
            id SERIAL PRIMARY KEY,
            word TEXT UNIQUE NOT NULL,
            CONSTRAINT word_length CHECK (length(word) BETWEEN 3 AND 32)
        )
    )");

    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS document_words (
            document_id INTEGER REFERENCES documents(id) ON DELETE CASCADE,
            word_id INTEGER REFERENCES words(id) ON DELETE CASCADE,
            count INTEGER NOT NULL,
            PRIMARY KEY (document_id, word_id)
        )
    )");

    // Индексы для ускорения поиска
    txn.exec("CREATE INDEX IF NOT EXISTS idx_word_text ON words(word)");
    txn.exec("CREATE INDEX IF NOT EXISTS idx_document_words_word ON document_words(word_id)");
    txn.exec("CREATE INDEX IF NOT EXISTS idx_document_words_doc ON document_words(document_id)");

    txn.commit();
}

void Database::saveDocument(const string& url, const string& title, const string& content) {
    work txn(conn_);

    // Вставка или обновление документа
    auto doc = txn.exec_params1(
        "INSERT INTO documents (url, title, content, last_crawled) "
        "VALUES ($1, $2, $3, NOW()) "
        "ON CONFLICT (url) DO UPDATE "
        "SET title = $2, content = $3, last_crawled = NOW() "
        "RETURNING id",
        url, title, content
    );
    int doc_id = doc[0].as<int>();

    // Очистка текста
    string text = regex_replace(content, regex("<[^>]*>"), " "); // Удаление HTML
    text = regex_replace(text, regex("[^\\w\\s]"), " "); // Удаление пунктуации
    text = bl::to_lower(bl::normalize(text)); // Нормализация и нижний регистр

    // Подсчет слов
    map<string, int> word_counts;
    vector<string> words;
    ba::split(words, text, ba::is_any_of(" \t\n\r"), ba::token_compress_on);

    for (const auto& word : words) {
        if (word.length() >= 3 && word.length() <= 32) {
            word_counts[word]++;
        }
    }

    // Сохранение слов и частотности
    for (const auto& [word, count] : word_counts) {
        // Вставка слова
        auto word_row = txn.exec_params1(
            "INSERT INTO words (word) VALUES ($1) "
            "ON CONFLICT (word) DO UPDATE SET word = $1 "
            "RETURNING id",
            word
        );
        int word_id = word_row[0].as<int>();

        // Связь документа и слова
        txn.exec_params(
            "INSERT INTO document_words (document_id, word_id, count) "
            "VALUES ($1, $2, $3) "
            "ON CONFLICT (document_id, word_id) DO UPDATE "
            "SET count = $3",
            doc_id, word_id, count
        );
    }

    txn.commit();
}

vector<tuple<string, string, int>> Database::search(const vector<string>& words) {
    work txn(conn_);

    // Формирование SQL-запроса
    string sql = R"(
        WITH matched_words AS (
            SELECT id FROM words WHERE word IN ()";

    // Добавление параметров для каждого слова
    for (size_t i = 0; i < words.size(); ++i) {
        sql += "$" + to_string(i + 1);
        if (i != words.size() - 1) sql += ",";
    }

    sql += R"()
        ),
        relevant_docs AS (
            SELECT dw.document_id, SUM(dw.count) as relevance
            FROM document_words dw
            JOIN matched_words mw ON dw.word_id = mw.id
            GROUP BY dw.document_id
            HAVING COUNT(DISTINCT dw.word_id) = )" + to_string(words.size()) + R"(
        )
        SELECT d.url, d.title, rd.relevance
        FROM documents d
        JOIN relevant_docs rd ON d.id = rd.document_id
        ORDER BY rd.relevance DESC
        LIMIT 10
    )";

    // Подготовка параметров
    vector<const char*> params;
    for (const auto& word : words) {
        params.push_back(word.c_str());
    }

    // Выполнение запроса
    auto result = txn.exec_params(sql, params);
    vector<tuple<string, string, int>> results;

    for (const auto& row : result) {
        results.emplace_back(
            row[0].as<string>(), // url
            row[1].as<string>(), // title
            row[2].as<int>()     // relevance
        );
    }

    return results;
}