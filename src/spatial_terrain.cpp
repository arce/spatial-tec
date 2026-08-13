#include <iostream>
#include <string>

void printUsage() {
  std::cerr << "spatial_terrain - Terrain analysis from a DEM: slope, aspect, hillshade, curvature [PLACEHOLDER, aun no implementado]\n\n";
  std::cerr << "Usage: spatial_terrain <input.asc> <output.asc> -operation <slope|aspect|hillshade|curvature> [options]\n\n";
  std::cerr << "Operations:\n";
  std::cerr << "  -operation slope        Slope raster\n";
  std::cerr << "  -operation aspect       Aspect (orientation) raster\n";
  std::cerr << "  -operation hillshade    Shaded relief raster\n";
  std::cerr << "  -operation curvature    Terrain curvature raster\n\n";
  std::cerr << "Options (apply only to the matching operation):\n";
  std::cerr << "  -units <degrees|percent>            Slope units (slope, default: degrees)\n";
  std::cerr << "  -z_factor <value>                   Vertical exaggeration (slope, hillshade; default: 1.0)\n";
  std::cerr << "  -flat_value <value>                 Value for flat cells (aspect; default: -1)\n";
  std::cerr << "  -azimuth <value>                    Light source azimuth in degrees (hillshade; default: 315)\n";
  std::cerr << "  -altitude <value>                   Light source altitude in degrees (hillshade; default: 45)\n";
  std::cerr << "  -type <profile|planform|general>    Curvature type (curvature; default: general)\n\n";
  std::cerr << "Status:\n";
  std::cerr << "  Este comando es un cascaron: valida y analiza sus argumentos igual que\n";
  std::cerr << "  el resto de Spatial TEC, pero todavia no ejecuta ningun calculo real.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_terrain dem.asc slope.asc -operation slope\n";
  std::cerr << "  spatial_terrain dem.asc slope.asc -operation slope -units percent\n";
  std::cerr << "  spatial_terrain dem.asc aspect.asc -operation aspect\n";
  std::cerr << "  spatial_terrain dem.asc hillshade.asc -operation hillshade -azimuth 315 -altitude 45\n";
  std::cerr << "  spatial_terrain dem.asc curvature.asc -operation curvature -type profile\n";
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    printUsage();
    return 1;
  }

  std::string input_file = argv[1];
  std::string output_file = argv[2];
  std::string operation;
  std::string units = "degrees";
  double z_factor = 1.0;
  double flat_value = -1.0;
  double azimuth = 315.0;
  double altitude = 45.0;
  std::string curv_type = "general";

  for (int i = 3; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-operation" && i + 1 < argc) {
      operation = argv[++i];
    } else if (arg == "-units" && i + 1 < argc) {
      units = argv[++i];
    } else if (arg == "-z_factor" && i + 1 < argc) {
      z_factor = std::stod(argv[++i]);
    } else if (arg == "-flat_value" && i + 1 < argc) {
      flat_value = std::stod(argv[++i]);
    } else if (arg == "-azimuth" && i + 1 < argc) {
      azimuth = std::stod(argv[++i]);
    } else if (arg == "-altitude" && i + 1 < argc) {
      altitude = std::stod(argv[++i]);
    } else if (arg == "-type" && i + 1 < argc) {
      curv_type = argv[++i];
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option: " << arg << "\n";
      printUsage();
      return 1;
    }
  }

  if (operation != "slope" && operation != "aspect" && operation != "hillshade" &&
      operation != "curvature") {
    std::cerr << "Error: -operation is required and must be one of: slope, aspect, hillshade, curvature\n";
    printUsage();
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  TERRAIN ANALYSIS: " << operation << "\n";
  std::cout << "========================================\n\n";
  std::cout << "input.asc: " << input_file << "\n";
  std::cout << "output.asc: " << output_file << "\n";
  std::cout << "-operation: " << operation << "\n";
  if (operation == "slope") {
    std::cout << "-units: " << units << "\n";
    std::cout << "-z_factor: " << z_factor << "\n";
  } else if (operation == "aspect") {
    std::cout << "-flat_value: " << flat_value << "\n";
  } else if (operation == "hillshade") {
    std::cout << "-azimuth: " << azimuth << "\n";
    std::cout << "-altitude: " << altitude << "\n";
    std::cout << "-z_factor: " << z_factor << "\n";
  } else if (operation == "curvature") {
    std::cout << "-type: " << curv_type << "\n";
  }

  std::cerr << "\n[spatial_terrain] AVISO: este comando es un cascaron (placeholder).\n";
  std::cerr << "Analiza correctamente sus argumentos, pero todavia no calcula ningun\n";
  std::cerr << "resultado real -- no se genero ningun archivo de salida.\n";
  std::cerr << "Consulte doc/commands/raster.md para el estado de esta funcionalidad.\n";
  return 2;
}

