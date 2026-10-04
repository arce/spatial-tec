#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_types.hpp"

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
    static const std::unordered_set<std::string> fns = {
        "sqrt", "abs", "sin", "cos", "tan", "log", "log10", "exp", "pow", "min", "max", "mean"
    };
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
    if (fn == "mean") {
      double sum = 0;
      for (double a : args) sum += a;
      return sum / args.size();
    }
    return 0.0;
  }
};

void printUsage() {
  std::cerr << "spatial_calc - Perform mathematical operations on rasters\n\n";
  std::cerr << "Usage: spatial_calc <input1> [input2 ...] <output> <expression>\n\n";
  std::cerr << "Expression syntax:\n";
  std::cerr << "  value           - Current cell value\n";
  std::cerr << "  raster1, raster2 - Input raster names (as given on command line)\n";
  std::cerr << "  Operators: +, -, *, /, ^, >, <, >=, <=, ==, !=\n";
  std::cerr << "  Functions: sqrt(), abs(), sin(), cos(), tan(), log(), log10()\n";
  std::cerr << "             exp(), pow(), min(), max(), mean()\n";
  std::cerr << "  Conditional: condition ? true_value : false_value\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_calc elev.asc elev_ft.asc \"value * 0.3048\"\n";
  std::cerr << "  spatial_calc band1.asc band2.asc result.asc \"raster1 + raster2\"\n";
  std::cerr << "  spatial_calc elev.asc slope.asc \"atan(value) * 57.2958\"\n";
  std::cerr << "  spatial_calc elev.asc mask.asc \"value > 20 ? 1 : 0\"\n";
  std::cerr << "  spatial_calc r1.asc r2.asc r3.asc result.asc \"(r1 + r2) / r3\"\n";
}

int main(int argc, char* argv[]) {
  if (argc < 4) {
    printUsage();
    return 1;
  }

  std::vector<std::string> input_files;
  std::string output_file;
  std::string expression;

  expression = argv[argc - 1];
  output_file = argv[argc - 2];

  for (int i = 1; i < argc - 2; ++i) {
    input_files.push_back(argv[i]);
  }

  if (input_files.empty()) {
    std::cerr << "Error: At least one input file required\n";
    return 1;
  }

  std::cout << "Input files: " << input_files.size() << "\n";
  for (const auto& f : input_files) {
    std::cout << "  " << f << "\n";
  }
  std::cout << "Output: " << output_file << "\n";
  std::cout << "Expression: " << expression << "\n\n";

  std::vector<Spatial::RasterDataset> rasters;
  Spatial::ASCIIGridReader reader;

  for (const auto& filename : input_files) {
    Spatial::RasterDataset dataset;
    if (!reader.read(filename, dataset)) {
      std::cerr << "Error: Could not read input file: " << filename << "\n";
      return 1;
    }
    rasters.push_back(dataset);
    std::cout << "Loaded: " << filename << " (" << dataset.ncols << "x" << dataset.nrows << ")\n";
  }

  for (size_t i = 1; i < rasters.size(); ++i) {
    if (rasters[i].ncols != rasters[0].ncols || rasters[i].nrows != rasters[0].nrows) {
      std::cerr << "Error: Rasters must have same dimensions\n";
      std::cerr << "  " << input_files[0] << ": " << rasters[0].ncols << "x" << rasters[0].nrows << "\n";
      std::cerr << "  " << input_files[i] << ": " << rasters[i].ncols << "x" << rasters[i].nrows << "\n";
      return 1;
    }

    if (std::abs(rasters[i].xllcorner - rasters[0].xllcorner) > 1e-9 ||
        std::abs(rasters[i].yllcorner - rasters[0].yllcorner) > 1e-9 ||
        std::abs(rasters[i].cellsize - rasters[0].cellsize) > 1e-9) {
      std::cerr << "Warning: Rasters have different georeferencing\n";
    }
  }

  Lexer lexer(expression);
  std::vector<Tok> tokens = lexer.tokenize();
  std::unordered_set<std::string> variables = ExprEvaluator::collectVariables(tokens);

  std::cout << "\nVariables: ";
  for (const auto& v : variables) {
    std::cout << v << " ";
  }
  std::cout << "\n\n";

  Spatial::RasterDataset output = rasters[0];
  output.data.resize(output.nrows * output.ncols);
  output.nodata_value = rasters[0].nodata_value;

  std::unordered_map<std::string, int> var_to_index;
  for (const auto& var : variables) {
    if (var == "value") {
      var_to_index[var] = -1;
    } else if (var == "raster" || var == "raster1") {
      var_to_index[var] = 0;
    } else if (var.find("raster") == 0) {
      int idx = std::stoi(var.substr(6)) - 1;
      if (idx >= 0 && idx < (int)rasters.size()) {
        var_to_index[var] = idx;
      } else {
        std::cerr << "Error: Invalid raster index: " << var << "\n";
        return 1;
      }
    } else {
      bool found = false;
      for (size_t i = 0; i < input_files.size(); ++i) {
        std::string basename = std::filesystem::path(input_files[i]).stem().string();
        if (basename == var) {
          var_to_index[var] = i;
          found = true;
          break;
        }
      }
      if (!found) {
        std::cerr << "Warning: Unknown variable: " << var << "\n";
        var_to_index[var] = 0;
      }
    }
  }

  ExprEvaluator evaluator(tokens);
  int processed = 0;
  int nodata_count = 0;

  std::cout << "Processing " << (output.nrows * output.ncols) << " cells...\n";

  for (int r = 0; r < output.nrows; ++r) {
    for (int c = 0; c < output.ncols; ++c) {
      bool has_nodata = false;
      for (const auto& raster : rasters) {
        if (std::abs(raster.at(r, c) - raster.nodata_value) < 1e-9) {
          has_nodata = true;
          break;
        }
      }

      if (has_nodata) {
        output.at(r, c) = output.nodata_value;
        nodata_count++;
        continue;
      }

      auto varLookup = [&](const std::string& name) -> double {
        auto it = var_to_index.find(name);
        if (it == var_to_index.end()) return 0.0;
        int idx = it->second;
        if (idx == -1) {
          return rasters[0].at(r, c);
        } else if (idx >= 0 && idx < (int)rasters.size()) {
          return rasters[idx].at(r, c);
        }
        return 0.0;
      };

      double result = evaluator.evaluate(varLookup);

      if (std::isnan(result) || std::isinf(result)) {
        output.at(r, c) = output.nodata_value;
        nodata_count++;
      } else {
        output.at(r, c) = result;
      }

      processed++;

      if (processed % 10000 == 0) {
        std::cout << "\r  Progress: " << (processed * 100 / (output.nrows * output.ncols)) << "%"
                  << std::flush;
      }
    }
  }

  std::cout << "\r  Progress: 100%\n";
  std::cout << "\nProcessed: " << processed << " cells\n";
  std::cout << "NODATA cells: " << nodata_count << "\n";

  writeRasterASCII(output, output_file);
  std::cout << "\nOutput written to: " << output_file << "\n";
  std::cout << "Calculation complete.\n";

  return 0;
}