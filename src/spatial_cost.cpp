#include <cctype>
#include <iostream>
#include <string>
#include <vector>

void printUsage() {
  std::cerr << "spatial_cost - Cost-distance, viewshed and least-cost corridor analysis [PLACEHOLDER, aun no implementado]\n\n";
  std::cerr << "Usage: spatial_cost <input1> [input2] <output> -operation <cost|viewshed|corridor> [options]\n\n";
  std::cerr << "Operations:\n";
  std::cerr << "  -operation cost        1 input (friction raster) -> accumulated cost surface\n";
  std::cerr << "  -operation viewshed    1 input (DEM) -> visibility mask from an observer point\n";
  std::cerr << "  -operation corridor    2 inputs (two accumulated cost surfaces) -> least-cost corridor\n\n";
  std::cerr << "Options (apply only to the matching operation):\n";
  std::cerr << "  -source <x> <y>       Origin point (cost; required for -operation cost)\n";
  std::cerr << "  -max_cost <value>     Maximum accumulated cost (cost; default: no limit)\n";
  std::cerr << "  -observer <x> <y>     Observer point (viewshed; required for -operation viewshed)\n";
  std::cerr << "  -obs_height <value>   Observer height above terrain (viewshed; default: 1.7)\n";
  std::cerr << "  -radius <value>       Maximum analysis radius (viewshed; default: no limit)\n";
  std::cerr << "  -threshold <value>    Corridor cost threshold (corridor; default: minimum + 10%)\n\n";
  std::cerr << "Status:\n";
  std::cerr << "  Este comando es un cascaron: valida y analiza sus argumentos igual que\n";
  std::cerr << "  el resto de Spatial TEC, pero todavia no ejecuta ningun calculo real.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_cost friction.asc cost.asc -operation cost -source -84.09 9.93\n";
  std::cerr << "  spatial_cost dem.asc view.asc -operation viewshed -observer -84.09 9.93\n";
  std::cerr << "  spatial_cost cost_a.asc cost_b.asc corridor.asc -operation corridor\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::vector<std::string> positionals;
  int first_option_idx = argc;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (!arg.empty() && arg[0] == '-' && !(arg.size() > 1 && (isdigit(arg[1]) || arg[1] == '.'))) {
      first_option_idx = i;
      break;
    }
    positionals.push_back(arg);
  }

  if (positionals.size() < 2) {
    std::cerr << "Error: expected at least <input> <output>\n";
    printUsage();
    return 1;
  }

  std::string output_file = positionals.back();
  std::vector<std::string> inputs(positionals.begin(), positionals.end() - 1);

  std::string operation;
  double source_x = 0.0, source_y = 0.0;
  bool source_set = false;
  double max_cost = -1.0;
  double observer_x = 0.0, observer_y = 0.0;
  bool observer_set = false;
  double obs_height = 1.7;
  double radius = -1.0;
  double threshold = -1.0;

  for (int i = first_option_idx; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-operation" && i + 1 < argc) {
      operation = argv[++i];
    } else if (arg == "-source" && i + 2 < argc) {
      source_x = std::stod(argv[++i]);
      source_y = std::stod(argv[++i]);
      source_set = true;
    } else if (arg == "-max_cost" && i + 1 < argc) {
      max_cost = std::stod(argv[++i]);
    } else if (arg == "-observer" && i + 2 < argc) {
      observer_x = std::stod(argv[++i]);
      observer_y = std::stod(argv[++i]);
      observer_set = true;
    } else if (arg == "-obs_height" && i + 1 < argc) {
      obs_height = std::stod(argv[++i]);
    } else if (arg == "-radius" && i + 1 < argc) {
      radius = std::stod(argv[++i]);
    } else if (arg == "-threshold" && i + 1 < argc) {
      threshold = std::stod(argv[++i]);
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  if (operation != "cost" && operation != "viewshed" && operation != "corridor") {
    std::cerr << "Error: -operation is required and must be one of: cost, viewshed, corridor\n";
    printUsage();
    return 1;
  }
  if (operation == "corridor" && inputs.size() != 2) {
    std::cerr << "Error: -operation corridor requires exactly two input rasters\n";
    printUsage();
    return 1;
  }
  if ((operation == "cost" || operation == "viewshed") && inputs.size() != 1) {
    std::cerr << "Error: -operation " << operation << " requires exactly one input raster\n";
    printUsage();
    return 1;
  }
  if (operation == "cost" && !source_set) {
    std::cerr << "Error: -source <x> <y> is required for -operation cost\n";
    printUsage();
    return 1;
  }
  if (operation == "viewshed" && !observer_set) {
    std::cerr << "Error: -observer <x> <y> is required for -operation viewshed\n";
    printUsage();
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  COST / VIEWSHED / CORRIDOR: " << operation << "\n";
  std::cout << "========================================\n\n";
  for (size_t i = 0; i < inputs.size(); ++i) {
    std::cout << "input[" << i << "]: " << inputs[i] << "\n";
  }
  std::cout << "output: " << output_file << "\n";
  std::cout << "-operation: " << operation << "\n";
  if (operation == "cost") {
    std::cout << "-source: " << source_x << " " << source_y << "\n";
    std::cout << "-max_cost: " << max_cost << "\n";
  } else if (operation == "viewshed") {
    std::cout << "-observer: " << observer_x << " " << observer_y << "\n";
    std::cout << "-obs_height: " << obs_height << "\n";
    std::cout << "-radius: " << radius << "\n";
  } else if (operation == "corridor") {
    std::cout << "-threshold: " << threshold << "\n";
  }

  std::cerr << "\n[spatial_cost] AVISO: este comando es un cascaron (placeholder).\n";
  std::cerr << "Analiza correctamente sus argumentos, pero todavia no calcula ningun\n";
  std::cerr << "resultado real -- no se genero ningun archivo de salida.\n";
  std::cerr << "Consulte doc/commands/raster.md para el estado de esta funcionalidad.\n";
  return 2;
}

