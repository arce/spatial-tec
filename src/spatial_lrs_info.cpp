#include <iostream>
#include <string>

void printUsage() {
  std::cerr << "spatial_lrs_info - LRS network information and validation [PLACEHOLDER, aun no implementado]\n\n";
  std::cerr << "Usage: spatial_lrs_info <network.lrs> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -route <id>       Show detail for a single route\n";
  std::cerr << "  -validate         Also validate topology and M-values (overlaps, gaps, out-of-range measures)\n";
  std::cerr << "  -report <file>    Save a report of the issues found (only meaningful with -validate)\n\n";
  std::cerr << "Status:\n";
  std::cerr << "  Este comando es un cascaron: valida y analiza sus argumentos igual que\n";
  std::cerr << "  el resto de Spatial TEC, pero todavia no ejecuta ningun calculo real.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_lrs_info roads.lrs\n";
  std::cerr << "  spatial_lrs_info roads.lrs -route 101\n";
  std::cerr << "  spatial_lrs_info roads.lrs -validate -report lrs_errors.csv\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string network_file = argv[1];
  std::string route_id;
  bool validate = false;
  std::string report_file;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-route" && i + 1 < argc) {
      route_id = argv[++i];
    } else if (arg == "-validate") {
      validate = true;
    } else if (arg == "-report" && i + 1 < argc) {
      report_file = argv[++i];
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  std::cout << "========================================\n";
  std::cout << "  LRS NETWORK INFO" << (validate ? " + VALIDATION" : "") << "\n";
  std::cout << "========================================\n\n";
  std::cout << "network.lrs: " << network_file << "\n";
  if (!route_id.empty())
    std::cout << "-route: " << route_id << "\n";
  std::cout << "-validate: " << (validate ? "yes" : "no") << "\n";
  if (!report_file.empty())
    std::cout << "-report: " << report_file << "\n";

  std::cerr << "\n[spatial_lrs_info] AVISO: este comando es un cascaron (placeholder).\n";
  std::cerr << "Analiza correctamente sus argumentos, pero todavia no lee ni valida\n";
  std::cerr << "ninguna red real -- no se genero ningun reporte.\n";
  std::cerr << "Consulte doc/commands/network.md para el estado de esta funcionalidad.\n";
  return 2;
}

