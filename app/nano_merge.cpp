// clang-format off
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
// clang-format on

#include <unistd.h>

namespace fs = std::filesystem;

namespace {

constexpr std::array kAllowedVariations = {
    std::string_view{"nominal"},
    std::string_view{"jes_up"},
    std::string_view{"jes_down"},
    std::string_view{"jer_up"},
    std::string_view{"jer_down"},
    std::string_view{"met_up"},
    std::string_view{"met_down"},
};

constexpr std::string_view kUsage = "Usage: nano_merge <output_dir>\n"
                                   "  <output_dir>: base Condor output directory; piece files are read from <output_dir>/pieces";

std::string shell_quote(const fs::path &path) {
  std::string s = path.string();
  std::string out = "'";
  for (const char c : s) {
    if (c == '\'') {
      out += "'\"'\"'";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

std::set<std::string> allowed_variations() {
  return {std::begin(kAllowedVariations), std::end(kAllowedVariations)};
}

fs::path make_output_root() {
  const char *tmp = std::getenv("TMPDIR");
  fs::path base = tmp != nullptr && tmp[0] != '\0' ? fs::path(tmp) : fs::path("/tmp");
  auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
  return base / ("nano_merge_" + std::to_string(::getpid()) + "_" + std::to_string(seconds));
}

struct PieceEntry {
  int index = 0;
  fs::path path;
};

using FileGroup = std::vector<PieceEntry>;

void run_hadd(const FileGroup &files, const fs::path &output) {
  std::vector<std::string> quoted;
  quoted.reserve(files.size());
  for (const auto &item : files) {
    quoted.push_back(shell_quote(item.path));
  }

  std::string cmd = "hadd -f " + shell_quote(output);
  for (const auto &item : quoted) {
    cmd += " " + item;
  }
  std::cout << "[step] Running: " << cmd << "\n";

  const int status = std::system(cmd.c_str());
  if (status != 0) {
    throw std::runtime_error("hadd failed for " + output.string() + " (status=" + std::to_string(status) + ")");
  }
}

void merge_or_copy(const FileGroup &files, const fs::path &output) {
  if (files.empty()) {
    return;
  }
  fs::create_directories(output.parent_path());
  if (files.size() == 1) {
    const auto &src = files.front().path;
    fs::copy_file(src, output, fs::copy_options::overwrite_existing);
    std::cout << "[step] Copied single file to " << output << "\n";
    return;
  }
  run_hadd(files, output);
}

void merge_group(const std::string &nickname, const std::string &variation, const FileGroup &files, const fs::path &output_root) {
  auto sorted_files = files;
  std::sort(sorted_files.begin(), sorted_files.end(), [](const PieceEntry &a, const PieceEntry &b) { return a.index < b.index; });
  const auto output = variation.empty() ? output_root / (nickname + ".root") : output_root / variation / (nickname + "_" + variation + ".root");
  std::cout << "[step] Merging " << sorted_files.size() << " files for "
            << (variation.empty() ? (nickname + " (nominal)") : (nickname + ", variation=" + variation)) << "\n"
            << "       output: " << output << "\n";
  merge_or_copy(sorted_files, output);
}

void copy_tree_contents(const fs::path &from, const fs::path &to) {
  fs::create_directories(to);
  for (const auto &entry : fs::recursive_directory_iterator(from)) {
    const auto relative = fs::relative(entry.path(), from);
    const auto target = to / relative;
    if (entry.is_directory()) {
      fs::create_directories(target);
      continue;
    }
    if (!entry.is_regular_file()) {
      continue;
    }
    fs::create_directories(target.parent_path());
    fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing);
  }
}

}  // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 2) {
      std::cerr << kUsage << "\n";
      return 1;
    }

    const fs::path output_dir = fs::path(argv[1]);
    if (!fs::exists(output_dir) || !fs::is_directory(output_dir)) {
      std::cerr << "Input path must be a directory: " << output_dir << "\n";
      return 1;
    }

    const fs::path pieces_dir = output_dir / "pieces";
    if (!fs::exists(pieces_dir) || !fs::is_directory(pieces_dir)) {
      std::cerr << "Missing pieces directory: " << pieces_dir << "\n";
      return 1;
    }

    const std::set<std::string> allowed = allowed_variations();
    // nickname -> files without variation
    std::map<std::string, FileGroup> no_variation;
    // variation -> nickname -> files
    std::map<std::string, std::map<std::string, FileGroup>> by_variation;
    std::size_t total_root = 0;

    // Pattern1: nickname_idx.root
    const std::regex no_var_re(R"(^(.*)_([0-9]+)\.root$)");
    // Pattern2: nickname_idx_variation.root
    const std::regex var_re(R"(^(.*)_([0-9]+)_([A-Za-z0-9_]+)\.root$)");

    std::cout << "Reading piece files from: " << pieces_dir << "\n";
    for (const auto &entry : fs::recursive_directory_iterator(pieces_dir)) {
      if (!entry.is_regular_file()) {
        continue;
      }
      if (entry.path().extension() != ".root") {
        continue;
      }

      const auto filename = entry.path().filename().string();
      std::smatch match;
      if (std::regex_match(filename, match, var_re)) {
        const std::string nickname = match[1].str();
        const int idx = std::stoi(match[2].str());
        const std::string variation = match[3].str();
        if (allowed.count(variation) == 0) {
          std::cerr << "Skipping unknown variation: " << filename << "\n";
          continue;
        }
        by_variation[variation][nickname].push_back({idx, entry.path()});
        ++total_root;
        continue;
      }
      if (std::regex_match(filename, match, no_var_re)) {
        const std::string nickname = match[1].str();
        const int idx = std::stoi(match[2].str());
        no_variation[nickname].push_back({idx, entry.path()});
        ++total_root;
        continue;
      }
      std::cerr << "Skipping unmatched file: " << filename << "\n";
    }

    if (total_root == 0) {
      std::cerr << "No matching ROOT files in: " << pieces_dir << "\n";
      return 1;
    }

    const fs::path output_root = make_output_root();
    fs::create_directories(output_root);
    std::cout << "Created output dir: " << output_root << "\n";
    std::size_t merged_count = 0;
    std::size_t copied_count = 0;
    std::size_t written_files = 0;

    for (const auto &[nickname, files] : no_variation) {
      if (files.empty()) {
        continue;
      }
      ++written_files;
      if (files.size() == 1) {
        ++copied_count;
      } else {
        ++merged_count;
      }
      merge_group(nickname, "", files, output_root);
    }

    for (const auto &[variation, per_nick] : by_variation) {
      std::cout << "Preparing variation: " << variation << "\n";
      for (const auto &[nickname, files] : per_nick) {
        if (files.empty()) {
          continue;
        }
        ++written_files;
        if (files.size() == 1) {
          ++copied_count;
        } else {
          ++merged_count;
        }
        merge_group(nickname, variation, files, output_root);
      }
    }

    std::cout << "Summary:\n";
    std::cout << "  input files: " << total_root << "\n";
    std::cout << "  merged groups: " << merged_count << "\n";
    std::cout << "  copied singleton groups: " << copied_count << "\n";
    std::cout << "  output files written: " << written_files << "\n";
    std::cout << "All outputs written under: " << output_root << "\n";
    std::cout << "[step] Copying merged outputs from temporary dir to final output dir\n"
              << "       from: " << output_root << "\n"
              << "       to:   " << output_dir << "\n";
    copy_tree_contents(output_root, output_dir);
    std::cout << "Final merged outputs copied under: " << output_dir << "\n";
    std::error_code cleanup_error;
    fs::remove_all(output_root, cleanup_error);
    if (cleanup_error) {
      std::cerr << "Warning: failed to remove temporary output dir " << output_root << ": " << cleanup_error.message() << "\n";
    } else {
      std::cout << "Removed temporary output dir: " << output_root << "\n";
    }
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "nano_merge failed: " << ex.what() << "\n";
    return 1;
  }
}
