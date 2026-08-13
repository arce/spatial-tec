#include <iostream>
#include <string>

void printUsage() {
  std::cerr << "spatial_hydrology - DEM hydrologic conditioning: fill sinks, flow direction, flow accumulation [PLACEHOLDER, aun no implementado]\n\n";
  std::cerr << "Usage: spatial_hydrology <input.asc> <output.asc> -operation <fill|flow_direction|flow_accumulation> [options]\n\n";
  std::cerr << "Operations:\n";
  std::cerr << "  -operation fill                Fill sinks (depressions) in a DEM\n";
  std::cerr << "  -operation flow_direction       D8 flow direction from an already-filled DEM\n";
  std::cerr << "  -operation flow_accumulation    Flow accumulation from a flow direction raster\n";
  std::cerr << "                                  (input.asc is the flow direction raster for this operation)\n\n";
  std::cerr << "Options (apply only to the matching operation):\n";
  std::cerr << "  -max_depth <value>   Maximum depth to fill (fill; default: no limit)\n";
  std::cerr << "  -method <d8>         Flow direction algorithm (flow_direction; default: d8)\n";
  std::cerr << "  -weights <file>      Optional weights raster (flow_accumulation; default: each cell weighs 1)\n";
  std::cerr << "  -verbose             Show detailed information\n\n";
  std::cerr << "Status:\n";
  std::cerr << "  Este comando es un cascaron: valida y analiza sus argumentos igual que\n";
  std::cerr << "  el resto de Spatial TEC, pero todavia no ejecuta ningun calculo real.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_hydrology dem.asc dem_filled.asc -operation fill\n";
  std::cerr << "  spatial_hydrology dem_filled.asc flowdir.asc -operation flow_direction\n";
  std::cerr << "  spatial_hydrology flowdir.asc flowacc.asc -operation flow_accumulation\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string operation;
  double max_depth = -1.0;
  std::string method = "d8";
  std::string weights_file;
  bool verbose = false;

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-operation" && i + 1 < argc) {
      operation = argv[++i];
    } else if (arg == "-max_depth" && i + 1 < argc) {
      max_depth = std::stod(argv[++i]);
    } else if (arg == "-method" && i + 1 < argc) {
      method = argv[++i];
    } else if (arg == "-weights" && i + 1 < argc) {
      weights_file = argv[++i];
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

  if (operation != "fill" && operation != "flow_direction" && operation != "flow_accumulation") {
    std::cerr << "Error: -operation is required and must be one of: fill, flow_direction, flow_accumulation\n";
    printUsage();
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  HYDROLOGY: " << operation << "\n";
  std::cout << "========================================\n\n";
  std::cout << "input.asc: " << input_file << "\n";
  std::cout << "output.asc: " << output_file << "\n";
  std::cout << "-operation: " << operation << "\n";
  if (operation == "fill") {
    std::cout << "-max_depth: " << max_depth << "\n";
  } else if (operation == "flow_direction") {
    std::cout << "-method: " << method << "\n";
  } else if (operation == "flow_accumulation" && !weights_file.empty()) {
    std::cout << "-weights: " << weights_file << "\n";
  }
  std::cout << "-verbose: " << (verbose ? "yes" : "no") << "\n";

  std::cerr << "\n[spatial_hydrology] AVISO: este comando es un cascaron (placeholder).\n";
  std::cerr << "Analiza correctamente sus argumentos, pero todavia no calcula ningun\n";
  std::cerr << "resultado real -- no se genero ningun archivo de salida.\n";
  std::cerr << "Consulte doc/commands/raster.md para el estado de esta funcionalidad.\n";
  return 2;
}

