#pragma once
#include <pqxx/pqxx>
#include <vector>
#include <string>
#include <tuple>

class Database {
public:
    Database(const std::string& host,
        const std::string& port,
        const std::string& dbname,
        const std::string& user,
        const std::string& password);

    void initializeSchema();
    void saveDocument(const std::string& url,
        const std::string& title,
        const std::string& content);

    std::vector<std::tuple<std::string, std::string, int>>
        search(const std::vector<std::string>& words);

private:
    pqxx::connection conn_;
};