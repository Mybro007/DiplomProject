#pragma once
#include "config_parser.h"
#include <fstream>
#include <sstream>
#include <algorithm>

ConfigParser::ConfigParser(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + filename);
    }

    std::string current_section;
    std::string line;

    while (std::getline(file, line)) {
        // Удаляем комментарии
        line = line.substr(0, line.find(';'));

        // Удаляем пробелы в начале и конце
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        if (line.empty()) continue;

        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
        }
        else {
            size_t eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);

                // Удаляем пробелы в ключе и значении
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);

                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                config_[current_section][key] = value;
            }
        }
    }
}

std::string ConfigParser::get(const std::string& section, const std::string& key) const {
    auto sec_it = config_.find(section);
    if (sec_it == config_.end()) {
        throw std::runtime_error("Section not found: " + section);
    }

    auto key_it = sec_it->second.find(key);
    if (key_it == sec_it->second.end()) {
        throw std::runtime_error("Key not found: " + key + " in section: " + section);
    }

    return key_it->second;
}

int ConfigParser::getInt(const std::string& section, const std::string& key) const {
    return std::stoi(get(section, key));
}