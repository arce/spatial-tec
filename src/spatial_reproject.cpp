#include <iostream>
#include <string>
#include <vector>

void printUsage() {
  std::cerr << "spatial_reproject - reproyecta datos vectoriales o raster entre sistemas de referencia de coordenadas [PLACEHOLDER, aun no implementado]\n";
  std::cerr << "\n";
  std::cerr << "Usage: spatial_reproject <input> <output> [options]\n";
  std::cerr << "\n";
  std::cerr << "Options:\n";
  std::cerr << "  -from <crs>       CRS de origen, ej. EPSG:4326 (requerido)\n";
  std::cerr << "  -to <crs>         CRS de destino, ej. EPSG:5367 (requerido)\n";
  std::cerr << "  -verbose          Mostrar informacion detallada\n";
  std::cerr << "\n";
  std::cerr << "Status:\n";
  std::cerr << "  Este comando es un cascaron: valida y analiza sus argumentos igual que\n";
  std::cerr << "  el resto de Spatial TEC, pero todavia no ejecuta ningun calculo real.\n";
  std::cerr << "  Hoy el CRS solo se guarda como una etiqueta de texto en la cabecera de los\n";
  std::cerr << "  archivos de Spatial TEC (ver spatial_types.hpp); ningun comando transforma\n";
  std::cerr << "  coordenadas todavia. Vea practicas/Tutorial11_Proyecciones.md.\n";
  std::cerr << "\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_reproject cities.csv cities_crtm05.csv -from EPSG:4326 -to EPSG:5367\n";
  std::cerr << "  spatial_reproject dem.asc dem_utm.asc -from EPSG:4326 -to EPSG:32616\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string from_crs = "";
  std::string to_crs = "";
  bool verbose = false;
  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-from" && i + 1 < argc) {
      from_crs = argv[++i];
    } else if (arg == "-to" && i + 1 < argc) {
      to_crs = argv[++i];
    } else if (arg == "-verbose") {
      verbose = true;
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }
  if (from_crs.empty()) {
    std::cerr << "Error: -from is required\n";
    printUsage();
    return 1;
  }
  if (to_crs.empty()) {
    std::cerr << "Error: -to is required\n";
    printUsage();
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  CRS REPROJECTION\n";
  std::cout << "========================================\n\n";
  std::cout << "input: " << input_file << "\n";
  std::cout << "output: " << output_file << "\n";
  std::cout << "-from: " << from_crs << "\n";
  std::cout << "-to: " << to_crs << "\n";
  std::cout << "-verbose: " << (verbose ? "yes" : "no") << "\n";

  std::cerr << "\n[spatial_reproject] AVISO: este comando es un cascaron (placeholder).\n";
  std::cerr << "Analiza correctamente sus argumentos, pero todavia no calcula ningun\n";
  std::cerr << "resultado real -- no se genero ningun archivo de salida.\n";
  std::cerr << "Consulte doc/commands/projection.md para el estado de esta funcionalidad.\n";
  return 2;
}

