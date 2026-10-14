#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "../include/core/spatial_csv.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_types.hpp"

void printUsage() {
  std::cerr << "spatial_calc_vector - calcula o transforma columnas de atributos de un CSV vectorial mediante una expresion\n";
  std::cerr << "\n";
  std::cerr << "Usage: spatial_calc_vector <input.csv> <output.csv> [options]\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "  -column <name>       Columna a crear o sobrescribir (requerido)\n";
  std::cerr << "  -expression <expr>   Expresion a evaluar por fila, ej. \"population / area\" (requerido)\n";
  std::cerr << "\n";
  std::cerr << "Expression syntax:\n";
  std::cerr << "  Operadores: +, -, *, /, ^, >, <, >=, <=, ==, !=, condicion ? si : no\n";
  std::cerr << "  Funciones: sqrt(), abs(), sin(), cos(), tan(), log(), log10(), exp(), pow(), min(), max()\n";
  std::cerr << "  Variables: cualquier nombre de columna de atributos presente en <input.csv>\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_calc_vector cities.csv cities_out.csv -column density -expression \"population / area\"\n";
  std::cerr << "  spatial_calc_vector cities.csv cities_out.csv -column tier -expression \"population > 1000000 ? 1 : 0\"\n";
}

enum class TokType { NUMBER, IDENT, OP, LPAREN, RPAREN, COMMA, QUESTION, COLON, END };

struct Tok {
  TokType type;
  std::string text;
  double num = 0.0;
};

class Lexer {
 public:
  explicit Lexer(const std::string& src) : s(src) {}

  std::vector<Tok> tokenize() {
    std::vector<Tok> out;
    size_t i = 0;
    while (i < s.size()) {
      char c = s[i];
      if (std::isspace(static_cast<unsigned char>(c))) {
        ++i;
        continue;
      }
      if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
        std::string num;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) {
          num += s[i++];
        }
        Tok t;
        t.type = TokType::NUMBER;
        t.num = std::stod(num);
        out.push_back(t);
        continue;
      }
      if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        std::string id;
        while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
          id += s[i++];
        }
        Tok t;
        t.type = TokType::IDENT;
        t.text = id;
        out.push_back(t);
        continue;
      }
      if (c == '(') {
        out.push_back({TokType::LPAREN, "(", 0});
        ++i;
        continue;
      }
      if (c == ')') {
        out.push_back({TokType::RPAREN, ")", 0});
        ++i;
        continue;
      }
      if (c == ',') {
        out.push_back({TokType::COMMA, ",", 0});
        ++i;
        continue;
      }
      if (c == '?') {
        out.push_back({TokType::QUESTION, "?", 0});
        ++i;
        continue;
      }
      if (c == ':') {
        out.push_back({TokType::COLON, ":", 0});
        ++i;
        continue;
      }
      if (c == '>' || c == '<' || c == '=' || c == '!') {
        std::string op(1, c);
        if (i + 1 < s.size() && s[i + 1] == '=') {
          op += '=';
          i += 2;
        } else {
          ++i;
        }
        out.push_back({TokType::OP, op, 0});
        continue;
      }
      if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
        out.push_back({TokType::OP, std::string(1, c), 0});
        ++i;
        continue;
      }

      ++i;
    }
    out.push_back({TokType::END, "", 0});
    return out;
  }

 private:
  std::string s;
};

class ExprEvaluator {
 public:
  explicit ExprEvaluator(const std::vector<Tok>& toks) : tokens(toks), pos(0) {}

  static std::unordered_set<std::string> collectVariables(const std::vector<Tok>& toks) {
    std::unordered_set<std::string> vars;
    for (size_t i = 0; i < toks.size(); ++i) {
      if (toks[i].type == TokType::IDENT) {
        bool is_call = (i + 1 < toks.size() && toks[i + 1].type == TokType::LPAREN);
        if (!is_call && !isFunctionName(toks[i].text)) {
          vars.insert(toks[i].text);
        }
      }
    }
    return vars;
  }

  static bool isFunctionName(const std::string& name) {
    static const std::unordered_set<std::string> fns = {"sqrt", "abs",  "sin", "cos",  "tan",
                                                          "log",  "log10", "exp", "pow", "min", "max"};
    return fns.count(name) > 0;
  }

  double evaluate(const std::function<double(const std::string&)>& varLookup) {
    lookup = &varLookup;
    pos = 0;
    return parseTernary();
  }

 private:
  const std::vector<Tok>& tokens;
  size_t pos;
  const std::function<double(const std::string&)>* lookup = nullptr;

  const Tok& cur() const { return tokens[pos]; }
  bool isOp(const std::string& op) const { return cur().type == TokType::OP && cur().text == op; }

  double parseTernary() {
    double cond = parseComparison();
    if (cur().type == TokType::QUESTION) {
      ++pos;

      double true_val = parseTernary();
      if (cur().type != TokType::COLON) {
        std::cerr << "Error: expected ':' in ternary expression\n";
        return cond != 0.0 ? true_val : 0.0;
      }
      ++pos;
      double false_val = parseTernary();
      return (cond != 0.0) ? true_val : false_val;
    }
    return cond;
  }

  double parseComparison() {
    double left = parseAdditive();
    while (cur().type == TokType::OP &&
           (cur().text == ">" || cur().text == "<" || cur().text == ">=" || cur().text == "<=" ||
            cur().text == "==" || cur().text == "!=")) {
      std::string op = cur().text;
      ++pos;
      double right = parseAdditive();
      if (op == ">") left = (left > right) ? 1.0 : 0.0;
      else if (op == "<") left = (left < right) ? 1.0 : 0.0;
      else if (op == ">=") left = (left >= right) ? 1.0 : 0.0;
      else if (op == "<=") left = (left <= right) ? 1.0 : 0.0;
      else if (op == "==") left = (std::fabs(left - right) < 1e-12) ? 1.0 : 0.0;
      else if (op == "!=") left = (std::fabs(left - right) >= 1e-12) ? 1.0 : 0.0;
    }
    return left;
  }

  double parseAdditive() {
    double left = parseMultiplicative();
    while (isOp("+") || isOp("-")) {
      std::string op = cur().text;
      ++pos;
      double right = parseMultiplicative();
      left = (op == "+") ? (left + right) : (left - right);
    }
    return left;
  }

  double parseMultiplicative() {
    double left = parseUnary();
    while (isOp("*") || isOp("/")) {
      std::string op = cur().text;
      ++pos;
      double right = parseUnary();
      if (op == "*") {
        left = left * right;
      } else {
        left = (std::fabs(right) < 1e-12) ? 0.0 : (left / right);
      }
    }
    return left;
  }

  double parseUnary() {
    if (isOp("-")) {
      ++pos;
      return -parseUnary();
    }
    if (isOp("+")) {
      ++pos;
      return parseUnary();
    }
    return parsePower();
  }

  double parsePower() {
    double base = parsePrimary();
    if (isOp("^")) {
      ++pos;
      double exponent = parseUnary();
      return std::pow(base, exponent);
    }
    return base;
  }

  double parsePrimary() {
    if (cur().type == TokType::NUMBER) {
      double v = cur().num;
      ++pos;
      return v;
    }
    if (cur().type == TokType::LPAREN) {
      ++pos;
      double v = parseTernary();
      if (cur().type == TokType::RPAREN) ++pos;
      return v;
    }
    if (cur().type == TokType::IDENT) {
      std::string name = cur().text;
      ++pos;
      if (cur().type == TokType::LPAREN) {
        ++pos;
        std::vector<double> args;
        if (cur().type != TokType::RPAREN) {
          args.push_back(parseTernary());
          while (cur().type == TokType::COMMA) {
            ++pos;
            args.push_back(parseTernary());
          }
        }
        if (cur().type == TokType::RPAREN) ++pos;
        return applyFunction(name, args);
      }
      return (*lookup)(name);
    }

    ++pos;
    return 0.0;
  }

  double applyFunction(const std::string& fn, const std::vector<double>& args) {
    if (args.empty()) return 0.0;
    if (fn == "sqrt") return std::sqrt(args[0]);
    if (fn == "abs") return std::fabs(args[0]);
    if (fn == "sin") return std::sin(args[0]);
    if (fn == "cos") return std::cos(args[0]);
    if (fn == "tan") return std::tan(args[0]);
    if (fn == "log") return std::log(args[0]);
    if (fn == "log10") return std::log10(args[0]);
    if (fn == "exp") return std::exp(args[0]);
    if (fn == "pow" && args.size() >= 2) return std::pow(args[0], args[1]);
    if (fn == "min") {
      double m = args[0];
      for (double a : args) m = std::min(m, a);
      return m;
    }
    if (fn == "max") {
      double m = args[0];
      for (double a : args) m = std::max(m, a);
      return m;
    }
    return 0.0;
  }
};

std::string formatNumber(double v) {
  if (std::isnan(v)) return "NaN";
  if (std::isinf(v)) return v > 0 ? "Inf" : "-Inf";
  std::ostringstream oss;
  oss.precision(10);
  oss << v;
  return oss.str();
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string column;
  std::string expression;
  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-column" && i + 1 < argc) {
      column = argv[++i];
    } else if (arg == "-expression" && i + 1 < argc) {
      expression = argv[++i];
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }
  if (column.empty()) {
    std::cerr << "Error: -column is required\n";
    printUsage();
    return 1;
  }
  if (expression.empty()) {
    std::cerr << "Error: -expression is required\n";
    printUsage();
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  VECTOR ATTRIBUTE CALCULATOR\n";
  std::cout << "========================================\n\n";
  std::cout << "input.csv: " << input_file << "\n";
  std::cout << "output.csv: " << output_file << "\n";
  std::cout << "-column: " << column << "\n";
  std::cout << "-expression: " << expression << "\n\n";

  Spatial::VectorDataset dataset;
  Spatial::SpatialCSVReader reader;
  if (!reader.read(input_file, dataset)) {
    std::cerr << "Error: Could not read input file: " << input_file << "\n";
    return 1;
  }
  std::cout << "Loaded " << dataset.features.size() << " features, " << dataset.columns.size()
            << " columns\n";

  Lexer lexer(expression);
  std::vector<Tok> tokens = lexer.tokenize();
  std::unordered_set<std::string> vars = ExprEvaluator::collectVariables(tokens);

  std::vector<std::string> missing;
  for (const auto& v : vars) {
    if (v == column) continue;
    bool found = false;
    for (const auto& c : dataset.columns) {
      if (c == v) {
        found = true;
        break;
      }
    }
    if (!found) missing.push_back(v);
  }
  if (!missing.empty()) {
    std::cerr << "Error: expression references unknown column(s): ";
    for (size_t i = 0; i < missing.size(); ++i) {
      std::cerr << missing[i] << (i + 1 < missing.size() ? ", " : "\n");
    }
    std::cerr << "Available columns: ";
    for (size_t i = 0; i < dataset.columns.size(); ++i) {
      std::cerr << dataset.columns[i] << (i + 1 < dataset.columns.size() ? ", " : "\n");
    }
    return 1;
  }

  bool column_exists = false;
  for (const auto& c : dataset.columns) {
    if (c == column) {
      column_exists = true;
      break;
    }
  }
  if (!column_exists) {

    size_t insert_at = dataset.columns.size();
    for (size_t i = 0; i < dataset.columns.size(); ++i) {
      if (dataset.columns[i] == dataset.geometry_column) {
        insert_at = i;
        break;
      }
    }
    dataset.columns.insert(dataset.columns.begin() + insert_at, column);
  }

  ExprEvaluator evaluator(tokens);
  size_t nan_count = 0;
  for (auto& feature : dataset.features) {
    auto lookupFn = [&](const std::string& name) -> double {
      auto it = feature.attributes.find(name);
      if (it == feature.attributes.end()) return 0.0;
      try {
        return std::stod(it->second);
      } catch (...) {
        return 0.0;
      }
    };
    double result = evaluator.evaluate(lookupFn);
    if (std::isnan(result) || std::isinf(result)) ++nan_count;
    feature.attributes[column] = formatNumber(result);
  }

  writeVectorCSV(dataset, output_file, {"# spatial_calc_vector: " + column + " = " + expression});

  std::cout << "\nProcessed: " << dataset.features.size() << " rows\n";
  if (nan_count > 0) {
    std::cout << "Rows with NaN/Inf result: " << nan_count << "\n";
  }
  std::cout << "Output written to: " << output_file << "\n";
  std::cout << "Calculation complete.\n";

  return 0;
}

