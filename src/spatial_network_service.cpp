#include <iostream>
#include <string>

void printUsage() {
  std::cerr << "spatial_network_service - Service area allocation and isochrones from a source node [PLACEHOLDER, aun no implementado]\n\n";
  std::cerr << "Usage: spatial_network_service <network.network> -from <node> -mode <alloc|iso> [options]\n\n";
  std::cerr << "Modes:\n";
  std::cerr << "  -mode alloc    Single service area under -max_cost\n";
  std::cerr << "  -mode iso      Isochrones (one polygon per value in -intervals)\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -from <node>          Origin node (required)\n";
  std::cerr << "  -mode <alloc|iso>     Service mode (required)\n";
  std::cerr << "  -max_cost <value>     Maximum accumulated cost (mode alloc; default: no limit)\n";
  std::cerr << "  -intervals <list>     Comma-separated cost/time breaks, e.g. 5,10,15 (mode iso; required for iso)\n";
  std::cerr << "  -output <file>        Output CSV file (default: service_result.csv)\n\n";
  std::cerr << "Status:\n";
  std::cerr << "  Este comando es un cascaron: valida y analiza sus argumentos igual que\n";
  std::cerr << "  el resto de Spatial TEC, pero todavia no ejecuta ningun calculo real.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_network_service roads.network -from 0 -mode alloc -max_cost 10 -output service_area.csv\n";
  std::cerr << "  spatial_network_service roads.network -from 0 -mode iso -intervals 5,10,15 -output isochrones.csv\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string network_file = argv[1];
  std::string from_node;
  std::string mode;
  double max_cost = -1.0;
  std::string intervals;
  std::string output_file = "service_result.csv";

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-from" && i + 1 < argc) {
      from_node = argv[++i];
    } else if (arg == "-mode" && i + 1 < argc) {
      mode = argv[++i];
    } else if (arg == "-max_cost" && i + 1 < argc) {
      max_cost = std::stod(argv[++i]);
    } else if (arg == "-intervals" && i + 1 < argc) {
      intervals = argv[++i];
    } else if (arg == "-output" && i + 1 < argc) {
      output_file = argv[++i];
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  if (from_node.empty()) {
    std::cerr << "Error: -from is required\n";
    printUsage();
    return 1;
  }
  if (mode != "alloc" && mode != "iso") {
    std::cerr << "Error: -mode is required and must be one of: alloc, iso\n";
    printUsage();
    return 1;
  }
  if (mode == "iso" && intervals.empty()) {
    std::cerr << "Error: -intervals is required for -mode iso\n";
    printUsage();
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  NETWORK SERVICE: " << mode << "\n";
  std::cout << "========================================\n\n";
  std::cout << "network.network: " << network_file << "\n";
  std::cout << "-from: " << from_node << "\n";
  std::cout << "-mode: " << mode << "\n";
  if (mode == "alloc") {
    std::cout << "-max_cost: " << max_cost << "\n";
  } else {
    std::cout << "-intervals: " << intervals << "\n";
  }
  std::cout << "-output: " << output_file << "\n";

  std::cerr << "\n[spatial_network_service] AVISO: este comando es un cascaron (placeholder).\n";
  std::cerr << "Analiza correctamente sus argumentos, pero todavia no calcula ningun\n";
  std::cerr << "resultado real -- no se genero " << output_file << ".\n";
  std::cerr << "Consulte doc/commands/network.md para el estado de esta funcionalidad.\n";
  return 2;
}

