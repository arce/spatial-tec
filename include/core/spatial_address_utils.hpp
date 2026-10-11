#pragma once
// Utilidades compartidas por las herramientas de direcciones
// (spatial_address, spatial_address_validate, spatial_address_clean).

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "spatial_string.hpp"

namespace addr {

// Abreviaturas canonicas de tipo de via y puntos cardinales.
// La clave va en minusculas y sin puntuacion.
inline const std::map<std::string, std::string>& streetAbbreviations() {
  static const std::map<std::string, std::string> m = {
      {"street", "St"},    {"st", "St"},    {"str", "St"},
      {"avenue", "Ave"},   {"ave", "Ave"},  {"av", "Av"},   {"avenida", "Av"},
      {"road", "Rd"},      {"rd", "Rd"},
      {"boulevard", "Blvd"}, {"blvd", "Blvd"},
      {"drive", "Dr"},     {"dr", "Dr"},
      {"lane", "Ln"},      {"ln", "Ln"},
      {"court", "Ct"},     {"ct", "Ct"},
      {"place", "Pl"},     {"pl", "Pl"},
      {"highway", "Hwy"},  {"hwy", "Hwy"},
      {"north", "N"},      {"n", "N"},
      {"south", "S"},      {"s", "S"},
      {"east", "E"},       {"e", "E"},
      {"west", "W"},       {"w", "W"},
  };
  return m;
}

// Quita BOM, signos de puntuacion sobrantes y colapsa espacios.
inline std::string cleanText(const std::string& in) {
  std::string s = in;
  if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
      (unsigned char)s[2] == 0xBF) {
    s = s.substr(3);
  }
  std::string out;
  bool last_space = true;
  for (char c : s) {
    unsigned char uc = (unsigned char)c;
    if (c == '.' || c == ';' || c == '\t' || c == '\r' || c == '\n') {
      c = (c == '.') ? '\0' : ' ';
      if (c == '\0') continue;
    }
    if (std::isspace(uc)) {
      if (!last_space) out += ' ';
      last_space = true;
    } else {
      out += c;
      last_space = false;
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

inline std::string titleCaseToken(const std::string& t) {
  std::string r = t;
  bool start = true;
  for (char& c : r) {
    if (start && std::isalpha((unsigned char)c)) {
      c = (char)std::toupper((unsigned char)c);
      start = false;
    } else {
      c = (char)std::tolower((unsigned char)c);
      if (c == '-' || c == '\'') start = true;
    }
  }
  return r;
}

// Capitaliza cada palabra: "mAIN   street" -> "Main Street"
inline std::string titleCase(const std::string& in) {
  std::istringstream iss(cleanText(in));
  std::string tok, out;
  while (iss >> tok) {
    if (!out.empty()) out += ' ';
    out += titleCaseToken(tok);
  }
  return out;
}

// Nombre de calle canonico: "main  STREET." -> "Main St"
inline std::string standardizeStreet(const std::string& in) {
  std::istringstream iss(cleanText(in));
  std::string tok, out;
  const auto& abbr = streetAbbreviations();
  while (iss >> tok) {
    auto it = abbr.find(toLower(tok));
    std::string rep = (it != abbr.end()) ? it->second : titleCaseToken(tok);
    if (!out.empty()) out += ' ';
    out += rep;
  }
  return out;
}

// Clave de comparacion: canonica y en minusculas.
inline std::string normalizeKey(const std::string& in) {
  return toLower(standardizeStreet(in));
}

// Similitud 0..1 basada en distancia de Levenshtein.
inline double similarity(const std::string& a, const std::string& b) {
  if (a == b) return 1.0;
  if (a.empty() || b.empty()) return 0.0;
  std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    std::swap(prev, cur);
  }
  double d = (double)prev[b.size()];
  return 1.0 - d / (double)std::max(a.size(), b.size());
}

// Entrecomilla un campo CSV si es necesario.
inline std::string csvEscape(const std::string& f) {
  if (f.find_first_of(",\"\n\r") == std::string::npos) return f;
  std::string r = "\"";
  for (char c : f) {
    if (c == '"') r += '"';
    r += c;
  }
  r += '"';
  return r;
}

}  // namespace addr
