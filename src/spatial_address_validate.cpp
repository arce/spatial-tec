#include <iostream>
#include <string>
#include <vector>

void printUsage() {
  std::cerr << "spatial_address_validate - valida los rangos de direcciones de una capa de calles (solapes, huecos, rangos invertidos) [PLACEHOLDER, aun no implementado]\n";
  std::cerr << "\n";
  std::cerr << "Usage: spatial_address_validate <streets.csv> [options]\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "  -check_overlaps   Detectar rangos de numeracion solapados\n";
  std::cerr << "  -check_gaps       Detectar huecos en la numeracion\n";
  std::cerr << "  -report <file>    Guardar un reporte de problemas encontrados\n";
  std::cerr << "\n";
  std::cerr << "Status:\n";
  std::cerr << "  Este comando es un cascaron: valida y analiza sus argumentos igual que\n";
  std::cerr << "  el resto de Spatial TEC, pero todavia no ejecuta ningun calculo real.\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_address_validate streets.csv -check_overlaps -check_gaps -report issues.csv\n";
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string streets_file = argv[1];
  bool check_overlaps = false;
  bool check_gaps = false;
  std::string report_file = "";
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-check_overlaps") {
      check_overlaps = true;
    } else if (arg == "-check_gaps") {
      check_gaps = true;
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
  std::cout << "  ADDRESS RANGE VALIDATION\n";
  std::cout << "========================================\n\n";
  std::cout << "streets.csv: " << streets_file << "\n";
  std::cout << "-check_overlaps: " << (check_overlaps ? "yes" : "no") << "\n";
  std::cout << "-check_gaps: " << (check_gaps ? "yes" : "no") << "\n";
  std::cout << "-report: " << report_file << "\n";

  std::cerr << "\n[spatial_address_validate] AVISO: este comando es un cascaron (placeholder).\n";
  std::cerr << "Analiza correctamente sus argumentos, pero todavia no calcula ningun\n";
  std::cerr << "resultado real -- no se genero ningun archivo de salida.\n";
  std::cerr << "Consulte doc/commands/network.md para el estado de esta funcionalidad.\n";
  return 2;
}

