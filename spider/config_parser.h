#pragma once
#include <string>
#include <map>
#include <vector>

class ConfigParser {
public:
    ConfigParser(const std::string& filename);

    std::string get(const std::string& section, const std::string& key) const;
    int getInt(const std::string& section, const std::string& key) const;

private:
    std::map<std::string, std::map<std::string, std::string>> config_;
};