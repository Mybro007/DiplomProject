#pragma once
#include <pqxx/pqxx>
#include <vector>
#include <string>
#include <tuple>

class Database {
public:
    Database(const std::string& conn_str);

    void initializeSchema();
    void saveDocument(const std::string& url,
        const std::string& title,
        const std::string& content);

    // Возвращает: url, title, relevance_score
    std::vector<std::tuple<std::string, std::string, int>>
        search(const std::vector<std::string>& words);

private:
    pqxx::connection conn_;
};