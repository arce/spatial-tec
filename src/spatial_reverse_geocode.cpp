#include <iostream>
#include <string>
#include <vector>

void printUsage() {
  std::cerr << "spatial_reverse_geocode - encuentra la direccion postal mas cercana a uno o mas puntos dados (geocodificacion inversa) [PLACEHOLDER, aun no implementado]\n";
  std::cerr << "\n";
  std::cerr << "Usage: spatial_reverse_geocode <streets.csv> <addresses.csv> <points.csv> <output.csv> [options]\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "  -max_dist <value>   Distancia maxima de busqueda (default: 50.0)\n";
  std::cerr << "\n";
  std::cerr << "Status:\n";
  std::cerr << "  Este comando es un cascaron: valida y analiza sus argumentos igual que\n";
  std::cerr << "  el resto de Spatial TEC, pero todavia no ejecuta ningun calculo real.\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_reverse_geocode streets.csv addresses.csv query_points.csv result.csv\n";
}

int main(int argc, char* argv[]) {
  if (argc < 5) {
    printUsage();
    return 1;
  }

  std::string streets_file = argv[1];
  std::string addresses_file = argv[2];
  std::string points_file = argv[3];
  std::string output_file = argv[4];
  double max_dist = 50.0;
  for (int i = 5; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-max_dist" && i + 1 < argc) {
      max_dist = std::stod(argv[++i]);
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
  std::cout << "  REVERSE GEOCODING\n";
  std::cout << "========================================\n\n";
  std::cout << "streets.csv: " << streets_file << "\n";
  std::cout << "addresses.csv: " << addresses_file << "\n";
  std::cout << "points.csv: " << points_file << "\n";
  std::cout << "output.csv: " << output_file << "\n";
  std::cout << "-max_dist: " << max_dist << "\n";

  std::cerr << "\n[spatial_reverse_geocode] AVISO: este comando es un cascaron (placeholder).\n";
  std::cerr << "Analiza correctamente sus argumentos, pero todavia no calcula ningun\n";
  std::cerr << "resultado real -- no se genero ningun archivo de salida.\n";
  std::cerr << "Consulte doc/commands/network.md para el estado de esta funcionalidad.\n";
  return 2;
}

