#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <stack>
#include <string>
#include <vector>

#include "../include/core/ascii_grid.hpp"
#include "../include/core/spatial_io.hpp"
#include "../include/core/spatial_types.hpp"

enum TokenType {
  TOKEN_NUMBER,
  TOKEN_VARIABLE,
  TOKEN_OPERATOR,
  TOKEN_FUNCTION,
  TOKEN_LPAREN,
  TOKEN_RPAREN,
  TOKEN_COMMA,
  TOKEN_CONDITIONAL,
  TOKEN_EOF
};

struct Token {
  TokenType type;
  std::string value;
  double number;
  int precedence;
  bool is_left_assoc;

  Token() : type(TOKEN_EOF), value(""), number(0), precedence(0), is_left_assoc(true) {}
  Token(TokenType t, const std::string& v)
      : type(t), value(v), number(0), precedence(0), is_left_assoc(true) {
    if (type == TOKEN_NUMBER) {
      number = std::stod(v);
    } else if (type == TOKEN_OPERATOR) {
      setOperatorPrecedence(v);
    }
  }

  void setOperatorPrecedence(const std::string& op) {
    if (op == "+" || op == "-") {
      precedence = 1;
      is_left_assoc = true;
    } else if (op == "*" || op == "/") {
      precedence = 2;
      is_left_assoc = true;
    } else if (op == "^") {
      precedence = 3;
      is_left_assoc = false;
    } else if (op == ">" || op == "<" || op == ">=" || op == "<=" || op == "==" || op == "!=") {
      precedence = 0;
      is_left_assoc = true;
    } else {
      precedence = 0;
      is_left_assoc = true;
    }
  }
};

class ExpressionParser {
private:
  std::string expr;
  size_t pos;
  std::vector<Token> tokens;
  std::vector<std::string> variables;

public:
  ExpressionParser(const std::string& expression) : expr(expression), pos(0) {}

  std::vector<std::string> getVariables() const {
    return variables;
  }

  std::vector<Token> tokenize() {
    tokens.clear();
    variables.clear();
    pos = 0;

    while (pos < expr.length()) {
      char c = expr[pos];

      if (isspace(c)) {
        pos++;
        continue;
      }

      if (isdigit(c) || c == '.') {
        std::string num;
        while (pos < expr.length() && (isdigit(expr[pos]) || expr[pos] == '.')) {
          num += expr[pos];
          pos++;
        }
        tokens.push_back(Token(TOKEN_NUMBER, num));
        continue;
      }

      if (isalpha(c) || c == '_') {
        std::string var;
        while (pos < expr.length() && (isalnum(expr[pos]) || expr[pos] == '_')) {
          var += expr[pos];
          pos++;
        }

        if (pos < expr.length() && expr[pos] == '(') {
          tokens.push_back(Token(TOKEN_FUNCTION, var));

          continue;
        }

        if (var == "value" || var == "raster" || var.find("raster") == 0) {
          if (std::find(variables.begin(), variables.end(), var) == variables.end()) {
            variables.push_back(var);
          }
        }

        tokens.push_back(Token(TOKEN_VARIABLE, var));
        continue;
      }

      if (c == '+' || c == '-' || c == '*' || c == '/' || c == '^') {
        tokens.push_back(Token(TOKEN_OPERATOR, std::string(1, c)));
        pos++;
        continue;
      }

      if (c == '(') {
        tokens.push_back(Token(TOKEN_LPAREN, "("));
        pos++;
        continue;
      }

      if (c == ')') {
        tokens.push_back(Token(TOKEN_RPAREN, ")"));
        pos++;
        continue;
      }

      if (c == ',') {
        tokens.push_back(Token(TOKEN_COMMA, ","));
        pos++;
        continue;
      }

      if (c == '?' || c == ':') {
        tokens.push_back(Token(TOKEN_CONDITIONAL, std::string(1, c)));
        pos++;
        continue;
      }

      if (c == '>' && pos + 1 < expr.length() && expr[pos + 1] == '=') {
        tokens.push_back(Token(TOKEN_OPERATOR, ">="));
        pos += 2;
        continue;
      }

      if (c == '<' && pos + 1 < expr.length() && expr[pos + 1] == '=') {
        tokens.push_back(Token(TOKEN_OPERATOR, "<="));
        pos += 2;
        continue;
      }

      if (c == '=' && pos + 1 < expr.length() && expr[pos + 1] == '=') {
        tokens.push_back(Token(TOKEN_OPERATOR, "=="));
        pos += 2;
        continue;
      }

      if (c == '!' && pos + 1 < expr.length() && expr[pos + 1] == '=') {
        tokens.push_back(Token(TOKEN_OPERATOR, "!="));
        pos += 2;
        continue;
      }

      if (c == '>' || c == '<') {
        tokens.push_back(Token(TOKEN_OPERATOR, std::string(1, c)));
        pos++;
        continue;
      }

      pos++;
    }

    tokens.push_back(Token(TOKEN_EOF, ""));
    return tokens;
  }

  std::vector<Token> shuntingYard(const std::vector<Token>& tokens) {
    std::vector<Token> output;
    std::stack<Token> op_stack;

    for (const auto& token : tokens) {
      if (token.type == TOKEN_NUMBER || token.type == TOKEN_VARIABLE) {
        output.push_back(token);
      } else if (token.type == TOKEN_FUNCTION) {
        op_stack.push(token);
      } else if (token.type == TOKEN_COMMA) {
        while (!op_stack.empty() && op_stack.top().type != TOKEN_LPAREN) {
          output.push_back(op_stack.top());
          op_stack.pop();
        }
      } else if (token.type == TOKEN_OPERATOR) {
        while (!op_stack.empty() && op_stack.top().type == TOKEN_OPERATOR &&
               ((token.is_left_assoc && token.precedence <= op_stack.top().precedence) ||
                (!token.is_left_assoc && token.precedence < op_stack.top().precedence))) {
          output.push_back(op_stack.top());
          op_stack.pop();
        }
        op_stack.push(token);
      } else if (token.type == TOKEN_LPAREN) {
        op_stack.push(token);
      } else if (token.type == TOKEN_RPAREN) {
        while (!op_stack.empty() && op_stack.top().type != TOKEN_LPAREN) {
          output.push_back(op_stack.top());
          op_stack.pop();
        }
        if (!op_stack.empty() && op_stack.top().type == TOKEN_LPAREN) {
          op_stack.pop();
        }
        if (!op_stack.empty() && op_stack.top().type == TOKEN_FUNCTION) {
          output.push_back(op_stack.top());
          op_stack.pop();
        }
      } else if (token.type == TOKEN_CONDITIONAL) {
        op_stack.push(token);
      }
    }

    while (!op_stack.empty()) {
      output.push_back(op_stack.top());
      op_stack.pop();
    }

    return output;
  }

  std::vector<Token> parse() {
    auto tokens = tokenize();
    return shuntingYard(tokens);
  }
};

class ExpressionEvaluator {
private:
  std::vector<Token> rpn;
  std::unordered_map<std::string, double> variables;
  std::stack<double> eval_stack;

  double applyFunction(const std::string& func, const std::vector<double>& args) {
    if (func == "sqrt")
      return std::sqrt(args[0]);
    if (func == "abs")
      return std::abs(args[0]);
    if (func == "sin")
      return std::sin(args[0]);
    if (func == "cos")
      return std::cos(args[0]);
    if (func == "tan")
      return std::tan(args[0]);
    if (func == "log")
      return std::log(args[0]);
    if (func == "log10")
      return std::log10(args[0]);
    if (func == "exp")
      return std::exp(args[0]);
    if (func == "pow")
      return std::pow(args[0], args[1]);
    if (func == "min") {
      double result = args[0];
      for (size_t i = 1; i < args.size(); ++i) result = std::min(result, args[i]);
      return result;
    }
    if (func == "max") {
      double result = args[0];
      for (size_t i = 1; i < args.size(); ++i) result = std::max(result, args[i]);
      return result;
    }
    if (func == "mean") {
      double sum = 0;
      for (double arg : args) sum += arg;
      return sum / args.size();
    }
    return 0;
  }

public:
  ExpressionEvaluator(const std::vector<Token>& rpn_expr) : rpn(rpn_expr) {}

  void setVariable(const std::string& name, double value) {
    variables[name] = value;
  }

  double evaluate() {
    while (!eval_stack.empty()) eval_stack.pop();

    for (const auto& token : rpn) {
      if (token.type == TOKEN_NUMBER) {
        eval_stack.push(token.number);
      } else if (token.type == TOKEN_VARIABLE) {
        auto it = variables.find(token.value);
        if (it != variables.end()) {
          eval_stack.push(it->second);
        } else {
          eval_stack.push(0);
        }
      } else if (token.type == TOKEN_OPERATOR) {
        if (eval_stack.size() < 2) {
          std::cerr << "Error: Not enough operands for operator " << token.value << "\n";
          return 0;
        }
        double b = eval_stack.top();
        eval_stack.pop();
        double a = eval_stack.top();
        eval_stack.pop();

        if (token.value == "+")
          eval_stack.push(a + b);
        else if (token.value == "-")
          eval_stack.push(a - b);
        else if (token.value == "*")
          eval_stack.push(a * b);
        else if (token.value == "/") {
          if (std::abs(b) < 1e-12) {
            eval_stack.push(0);
          } else {
            eval_stack.push(a / b);
          }
        } else if (token.value == "^")
          eval_stack.push(std::pow(a, b));
        else if (token.value == ">")
          eval_stack.push(a > b ? 1.0 : 0.0);
        else if (token.value == "<")
          eval_stack.push(a < b ? 1.0 : 0.0);
        else if (token.value == ">=")
          eval_stack.push(a >= b ? 1.0 : 0.0);
        else if (token.value == "<=")
          eval_stack.push(a <= b ? 1.0 : 0.0);
        else if (token.value == "==")
          eval_stack.push(std::abs(a - b) < 1e-12 ? 1.0 : 0.0);
        else if (token.value == "!=")
          eval_stack.push(std::abs(a - b) >= 1e-12 ? 1.0 : 0.0);
      } else if (token.type == TOKEN_FUNCTION) {
        std::vector<double> args;

        if (token.value == "pow" || token.value == "min" || token.value == "max") {
          if (eval_stack.size() < 2) {
            std::cerr << "Error: Not enough arguments for function " << token.value << "\n";
            return 0;
          }
          double b = eval_stack.top();
          eval_stack.pop();
          double a = eval_stack.top();
          eval_stack.pop();
          args = {a, b};
        } else {
          if (eval_stack.empty()) {
            std::cerr << "Error: Not enough arguments for function " << token.value << "\n";
            return 0;
          }
          double a = eval_stack.top();
          eval_stack.pop();
          args = {a};
        }
        eval_stack.push(applyFunction(token.value, args));
      } else if (token.type == TOKEN_CONDITIONAL) {
        if (token.value == "?") {
        }
      }
    }

    if (eval_stack.empty())
      return 0;
    return eval_stack.top();
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
      std::cerr << "  " << input_files[0] << ": " << rasters[0].ncols << "x" << rasters[0].nrows
                << "\n";
      std::cerr << "  " << input_files[i] << ": " << rasters[i].ncols << "x" << rasters[i].nrows
                << "\n";
      return 1;
    }

    if (std::abs(rasters[i].xllcorner - rasters[0].xllcorner) > 1e-9 ||
        std::abs(rasters[i].yllcorner - rasters[0].yllcorner) > 1e-9 ||
        std::abs(rasters[i].cellsize - rasters[0].cellsize) > 1e-9) {
      std::cerr << "Warning: Rasters have different georeferencing\n";
      std::cerr << "  " << input_files[0] << ": xll=" << rasters[0].xllcorner
                << " yll=" << rasters[0].yllcorner << " cs=" << rasters[0].cellsize << "\n";
      std::cerr << "  " << input_files[i] << ": xll=" << rasters[i].xllcorner
                << " yll=" << rasters[i].yllcorner << " cs=" << rasters[i].cellsize << "\n";
    }
  }

  ExpressionParser parser(expression);
  auto rpn = parser.parse();
  auto variables = parser.getVariables();

  std::cout << "\nVariables: ";
  for (const auto& v : variables) {
    std::cout << v << " ";
  }
  std::cout << "\n";

  std::cout << "RPN tokens: ";
  for (const auto& t : rpn) {
    if (t.type == TOKEN_NUMBER)
      std::cout << t.number << " ";
    else if (t.type == TOKEN_VARIABLE)
      std::cout << t.value << " ";
    else if (t.type == TOKEN_OPERATOR)
      std::cout << t.value << " ";
    else if (t.type == TOKEN_FUNCTION)
      std::cout << t.value << "() ";
  }
  std::cout << "\n\n";

  Spatial::RasterDataset output = rasters[0];
  output.data.resize(output.nrows * output.ncols);
  output.nodata_value = rasters[0].nodata_value;

  std::unordered_map<std::string, int> var_to_index;
  for (size_t i = 0; i < variables.size(); ++i) {
    const std::string& var = variables[i];
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

  ExpressionEvaluator evaluator(rpn);
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

      for (const auto& [var, idx] : var_to_index) {
        double val;
        if (idx == -1) {
          val = rasters[0].at(r, c);
        } else if (idx >= 0 && idx < (int)rasters.size()) {
          val = rasters[idx].at(r, c);
        } else {
          val = 0;
        }
        evaluator.setVariable(var, val);
      }

      double result = evaluator.evaluate();

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

