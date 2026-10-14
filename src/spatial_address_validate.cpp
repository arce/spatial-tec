#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "../include/core/spatial_streets.hpp"

using streets::csvQuote;
using streets::Segment;

struct Issue {
  int row;
  std::string street;
  std::string severity;  // ERROR | WARNING
  std::string code;
  std::string detail;
};

static std::string sideName(bool left) {
  return left ? "left" : "right";
}

static std::string rangeText(const streets::Range& r) {
  return std::to_string(r.from) + "-" + std::to_string(r.to);
}

// A numeric interval of one side of one segment, used for overlap/gap checks.
struct Interval {
  int lo, hi, row;
  bool left;
};

static void printUsage() {
  std::cerr << "spatial_address_validate - Validate the numbering ranges of a street layer\n\n";
  std::cerr << "Usage: spatial_address_validate <streets.csv> [options]\n\n";
  std::cerr << "Options:\n";
  std::cerr << "  -check_ranges     Check each range on its own (missing, negative, descending, parity)\n";
  std::cerr << "  -check_overlaps   Detect overlapping ranges between segments of the same street\n";
  std::cerr << "  -check_gaps       Detect gaps in the numbering of a street\n";
  std::cerr << "  -report <file>    Save the issues found as a CSV (row,street_name,severity,issue,detail)\n\n";
  std::cerr << "Without any -check_* option all three checks are run.\n\n";
  std::cerr << "Issues (E = error, W = warning):\n";
  std::cerr << "  EMPTY_NAME (E)          segment without street name\n";
  std::cerr << "  BAD_GEOMETRY (E)        geometry with fewer than 2 vertices\n";
  std::cerr << "  NO_RANGE (E)            segment without numbering on either side\n";
  std::cerr << "  NEGATIVE_NUMBER (E)     negative number in a range\n";
  std::cerr << "  PARITY_MISMATCH (E)     range ends do not match the declared left_parity/right_parity\n";
  std::cerr << "  DESCENDING_RANGE (W)    from is greater than to (numbering decreases along the line)\n";
  std::cerr << "  SAME_PARITY_SIDES (W)   both sides use the same parity: sides cannot be told apart\n";
  std::cerr << "  OVERLAP (E)             numbers covered by two segments of the same street\n";
  std::cerr << "  GAP (W)                 numbers of a street not covered by any segment\n\n";
  std::cerr << "Segments are grouped by street_name, city and postal_code. Rows are counted from 1\n";
  std::cerr << "(first data row after the header).\n";
  std::cerr << "Exit code: 0 = OK (warnings allowed), 1 = usage/IO error or errors found.\n\n";
  std::cerr << "Examples:\n";
  std::cerr << "  spatial_address_validate streets.csv\n";
  std::cerr << "  spatial_address_validate streets.csv -check_overlaps -check_gaps -report issues.csv\n";
}

int main(int argc, char* argv[]) {
  if (argc >= 2 && (std::string(argv[1]) == "-help" || std::string(argv[1]) == "-h")) {
    printUsage();
    return 0;
  }
  if (argc < 2) {
    printUsage();
    return 1;
  }

  std::string streets_file = argv[1];
  bool check_ranges = false, check_overlaps = false, check_gaps = false;
  std::string report_file;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-check_ranges") {
      check_ranges = true;
    } else if (arg == "-check_overlaps") {
      check_overlaps = true;
    } else if (arg == "-check_gaps") {
      check_gaps = true;
    } else if (arg == "-report" && i + 1 < argc) {
      report_file = argv[++i];
    } else if (arg == "-help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      std::cerr << "Error: Unknown option or missing value: " << arg << "\n";
      printUsage();
      return 1;
    }
  }
  if (!check_ranges && !check_overlaps && !check_gaps)
    check_ranges = check_overlaps = check_gaps = true;

  std::string err;
  std::vector<Segment> segs;
  if (!streets::readStreets(streets_file, segs, err)) {
    std::cerr << "Error: " << err << "\n";
    return 1;
  }

  std::cout << "========================================\n";
  std::cout << "  STREET LAYER VALIDATION\n";
  std::cout << "========================================\n\n";
  std::cout << "Streets: " << streets_file << "\n";
  std::cout << "Segments: " << segs.size() << "\n";
  std::cout << "Checks:" << (check_ranges ? " ranges" : "") << (check_overlaps ? " overlaps" : "")
            << (check_gaps ? " gaps" : "") << "\n\n";

  std::vector<Issue> issues;
  auto add = [&](const Segment& s, const char* sev, const char* code, const std::string& detail) {
    issues.push_back({s.row, s.name, sev, code, detail});
  };

  if (check_ranges) {
    for (const auto& s : segs) {
      if (s.name.empty())
        add(s, "ERROR", "EMPTY_NAME", "street_name is empty");
      if (s.vertexCount() < 2)
        add(s, "ERROR", "BAD_GEOMETRY", "geometry has fewer than 2 vertices");
      if (!s.left.valid && !s.right.valid)
        add(s, "ERROR", "NO_RANGE", "no numbering on either side");

      for (int side = 0; side < 2; ++side) {
        bool left = side == 0;
        const streets::Range& r = left ? s.left : s.right;
        bool explicit_parity = left ? s.explicit_left_parity : s.explicit_right_parity;
        if (!r.valid)
          continue;
        std::string where = sideName(left) + " side " + rangeText(r) + ": ";
        if (r.from < 0 || r.to < 0)
          add(s, "ERROR", "NEGATIVE_NUMBER", where + "negative number");
        if (r.from > r.to)
          add(s, "WARNING", "DESCENDING_RANGE", where + "from is greater than to");
        if (explicit_parity && r.parity != "both") {
          bool from_ok = (r.from % 2 == 0) == (r.parity == "even");
          bool to_ok = (r.to % 2 == 0) == (r.parity == "even");
          if (!from_ok || !to_ok)
            add(s, "ERROR", "PARITY_MISMATCH",
                where + "declared parity is " + r.parity + " but the range ends are not " + r.parity);
        }
      }
      if (s.left.valid && s.right.valid && s.left.parity == s.right.parity && s.left.parity != "both")
        add(s, "WARNING", "SAME_PARITY_SIDES",
            "left " + rangeText(s.left) + " and right " + rangeText(s.right) + " are both " + s.left.parity);
    }
  }

  if (check_overlaps || check_gaps) {
    // Group segments of the same street and split its intervals by parity class.
    std::map<std::string, std::vector<const Segment*>> groups;
    for (const auto& s : segs) {
      if (s.name.empty())
        continue;
      groups[streets::normalizeName(s.name) + "|" + streets::normalizeName(s.city) + "|" +
             streets::normalizeName(s.postal)]
          .push_back(&s);
    }

    for (const auto& [key, members] : groups) {
      std::map<std::string, std::vector<Interval>> by_parity;
      for (const Segment* s : members) {
        if (s->left.valid)
          by_parity[s->left.parity].push_back({s->left.lo(), s->left.hi(), s->row, true});
        if (s->right.valid)
          by_parity[s->right.parity].push_back({s->right.lo(), s->right.hi(), s->row, false});
      }
      const Segment& first = *members.front();

      for (auto& [parity, ivs] : by_parity) {
        std::sort(ivs.begin(), ivs.end(), [](const Interval& a, const Interval& b) { return a.lo < b.lo; });
        int step = parity == "both" ? 1 : 2;
        std::string label = parity == "both" ? "numbers" : parity + " numbers";

        if (check_overlaps) {
          // Compare every pair (small groups), so overlaps are never missed
          // because of a long interval hiding a later one.
          for (size_t i = 0; i < ivs.size(); ++i)
            for (size_t j = i + 1; j < ivs.size(); ++j) {
              if (ivs[j].lo > ivs[i].hi)
                break;
              if (ivs[i].row == ivs[j].row)
                continue;  // two sides of one segment: handled by SAME_PARITY_SIDES
              int lo = std::max(ivs[i].lo, ivs[j].lo), hi = std::min(ivs[i].hi, ivs[j].hi);
              issues.push_back({ivs[j].row, first.name, "ERROR", "OVERLAP",
                                label + " " + std::to_string(lo) + "-" + std::to_string(hi) +
                                    " covered by rows " + std::to_string(ivs[i].row) + " and " +
                                    std::to_string(ivs[j].row)});
            }
        }

        if (check_gaps) {
          int covered_to = ivs.front().hi;
          for (size_t i = 1; i < ivs.size(); ++i) {
            if (ivs[i].lo - covered_to > step)
              issues.push_back({ivs[i].row, first.name, "WARNING", "GAP",
                                label + " " + std::to_string(covered_to + step) + "-" +
                                    std::to_string(ivs[i].lo - step) + " are not covered (before row " +
                                    std::to_string(ivs[i].row) + ")"});
            covered_to = std::max(covered_to, ivs[i].hi);
          }
        }
      }
    }
  }

  int errors = 0, warnings = 0;
  std::cout << "Validation:\n";
  if (issues.empty())
    std::cout << "  No issues found.\n";
  for (const auto& is : issues) {
    (is.severity == "ERROR" ? errors : warnings)++;
    std::cout << "  [" << is.severity << "] row " << is.row << " (" << is.street << "): " << is.code << " - "
              << is.detail << "\n";
  }
  std::cout << "  Errors: " << errors << ", Warnings: " << warnings << "\n";

  if (!report_file.empty()) {
    std::ofstream rep(report_file);
    if (!rep.is_open()) {
      std::cerr << "Error: Could not write report: " << report_file << "\n";
      return 1;
    }
    rep << "row,street_name,severity,issue,detail\n";
    for (const auto& is : issues)
      rep << is.row << "," << csvQuote(is.street) << "," << is.severity << "," << is.code << ","
          << csvQuote(is.detail) << "\n";
    std::cout << "  Report written to: " << report_file << "\n";
  }
  std::cout << "\n";
  return errors > 0 ? 1 : 0;
}
