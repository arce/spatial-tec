#pragma once
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cctype>

inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

inline std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

inline std::string toUpper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

inline bool isNumber(const std::string& s) {
    if (s.empty()) return false;
    try {
        std::stod(s);
        return true;
    } catch (...) {
        return false;
    }
}

inline std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        std::string trimmed = trim(token);
        if (!trimmed.empty()) tokens.push_back(trimmed);
    }
    return tokens;
}

inline std::vector<std::string> split(const std::string& s, const std::string& delimiter) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    std::string str = s;
    while ((pos = str.find(delimiter)) != std::string::npos) {
        std::string token = str.substr(0, pos);
        if (!trim(token).empty()) tokens.push_back(trim(token));
        str.erase(0, pos + delimiter.length());
    }
    if (!trim(str).empty()) tokens.push_back(trim(str));
    return tokens;
}

inline std::vector<std::string> splitCSV(const std::string& line) {
    std::vector<std::string> result;
    std::string current;
    bool in_quotes = false;
    int paren_depth = 0;

    for (char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == '(' && !in_quotes) {
            paren_depth++;
            current += c;
        } else if (c == ')' && !in_quotes) {
            if (paren_depth > 0) paren_depth--;
            current += c;
        } else if (c == ',' && !in_quotes && paren_depth == 0) {
            result.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }
    result.push_back(trim(current));
    return result;
}
