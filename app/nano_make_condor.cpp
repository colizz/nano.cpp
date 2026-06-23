#include "runtime_common.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

struct CliOptions {
  std::string input_yaml;
  std::string job_dir;
  std::string output_dir;
  std::string config_file;
  std::string channel = "muon";
  std::string tree_name = "Events";
  std::string variations;
  long long num_events = -1;
  std::size_t nfiles_per_job = 1;
  std::unordered_map<std::string, std::string> overrides;
};

CliOptions parse_args(int argc, char **argv) {
  CliOptions opts;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto need_value = [&](const char *name) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error(std::string("Missing value for ") + name);
      }
      return argv[++i];
    };
    if (arg == "--input-yaml") {
      opts.input_yaml = need_value("--input-yaml");
    } else if (arg == "--job-dir") {
      opts.job_dir = need_value("--job-dir");
    } else if (arg == "--output-dir") {
      opts.output_dir = need_value("--output-dir");
    } else if (arg == "--config") {
      opts.config_file = need_value("--config");
    } else if (arg == "--channel") {
      opts.channel = need_value("--channel");
    } else if (arg == "--tree-name") {
      opts.tree_name = need_value("--tree-name");
    } else if (arg == "--variations") {
      opts.variations = need_value("--variations");
    } else if (arg == "--num-events") {
      opts.num_events = std::stoll(need_value("--num-events"));
    } else if (arg == "--nfiles-per-job") {
      opts.nfiles_per_job = static_cast<std::size_t>(std::stoul(need_value("--nfiles-per-job")));
    } else if (arg == "--set") {
      const auto kv = need_value("--set");
      const auto pos = kv.find('=');
      if (pos == std::string::npos) {
        throw std::runtime_error("--set expects key=value");
      }
      opts.overrides[kv.substr(0, pos)] = kv.substr(pos + 1);
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }
  if (opts.input_yaml.empty() || opts.job_dir.empty() || opts.output_dir.empty() || opts.config_file.empty()) {
    throw std::runtime_error("Usage: nano_make_condor --input-yaml <samples.yaml> --job-dir <condor-dir> --output-dir <dir> --config <card.yaml> [--nfiles-per-job 1] [--variations nominal,jes_up,...]");
  }
  return opts;
}

std::string write_merged_config(const fs::path &path, const YAML::Node &settings) {
  nano::runtime::dump_yaml_file(settings, path.string());
  return path.string();
}

std::string read_text_file(const fs::path &path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Failed to read template: " + path.string());
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void write_text_file(const fs::path &path, const std::string &content) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("Failed to write file: " + path.string());
  }
  output << content;
}

void replace_all(std::string &text, const std::string &from, const std::string &to) {
  std::size_t pos = 0;
  while ((pos = text.find(from, pos)) != std::string::npos) {
    text.replace(pos, from.size(), to);
    pos += to.size();
  }
}

std::string render_template(const fs::path &path, const std::map<std::string, std::string> &replacements) {
  auto text = read_text_file(path);
  for (const auto &[key, value] : replacements) {
    replace_all(text, key, value);
  }
  return text;
}

std::string json_escape(const std::string &value) {
  std::string out;
  for (const char ch : value) {
    switch (ch) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += ch;
        break;
    }
  }
  return out;
}

std::string nickname_from_dataset(const std::string &dataset, const std::string &fallback) {
  if (!dataset.empty() && dataset.front() == '/') {
    const auto second_slash = dataset.find('/', 1);
    if (second_slash != std::string::npos && second_slash > 1) {
      return dataset.substr(1, second_slash - 1);
    }
  }
  return fallback;
}

std::string safe_file_stem(std::string value) {
  for (auto &ch : value) {
    const auto uch = static_cast<unsigned char>(ch);
    if (!std::isalnum(uch) && ch != '_' && ch != '-' && ch != '.') {
      ch = '_';
    }
  }
  return value;
}

struct JobSpec {
  std::size_t index = 0;
  std::string nickname;
  std::size_t nickname_index = 0;
  std::vector<std::string> inputs;
  std::string output_file;
};

void write_job_manifest(const fs::path &path, const std::vector<JobSpec> &jobs) {
  std::ofstream out(path);
  out << "{\n";
  out << "  \"jobs\": [\n";
  for (std::size_t i = 0; i < jobs.size(); ++i) {
    const auto &job = jobs[i];
    out << "    {\n";
    out << "      \"index\": " << job.index << ",\n";
    out << "      \"nickname\": \"" << json_escape(job.nickname) << "\",\n";
    out << "      \"nickname_index\": " << job.nickname_index << ",\n";
    out << "      \"output_file\": \"" << json_escape(job.output_file) << "\",\n";
    out << "      \"inputs\": [\n";
    for (std::size_t j = 0; j < job.inputs.size(); ++j) {
      out << "        \"" << json_escape(job.inputs[j]) << "\"";
      if (j + 1 != job.inputs.size()) {
        out << ",";
      }
      out << "\n";
    }
    out << "      ]\n";
    out << "    }";
    if (i + 1 != jobs.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
}

void write_process_script(const fs::path &path, const fs::path &template_dir) {
  write_text_file(path, read_text_file(template_dir / "process.sh.in"));
  fs::permissions(path, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_exec |
                            fs::perms::group_read | fs::perms::others_exec | fs::perms::others_read,
                  fs::perm_options::add);
}

}  // namespace

int main(int argc, char **argv) {
  try {
    const auto cli = parse_args(argc, argv);
    auto settings = nano::runtime::load_config_with_extends(cli.config_file);
    for (const auto &[key, value] : cli.overrides) {
      nano::runtime::apply_override(settings, key, value);
    }

    const auto workdir = fs::path(cli.job_dir);
    fs::create_directories(workdir);
    const auto template_dir = fs::path("templates") / "condor";

    const auto merged_config = write_merged_config(workdir / "config_snapshot.yaml", settings);
    write_process_script(workdir / "process.sh", template_dir);

    const auto tarball = (workdir / "repo.tar.gz").string();
    const auto tar_cmd = "tar czf " + tarball +
                         " --exclude='./.git'"
                         " --exclude='./run'"
                         " --exclude='./jobs'"
                         " --exclude='./build'"
                         " --exclude='./build-*'"
                         " --exclude='./cmake-build-*'"
                         " --exclude='./tests/data/muon_validation'"
                         " --exclude='./external/CMSJMECalculators/tests'"
                         " .";
    const auto rc = std::system(tar_cmd.c_str());
    if (rc != 0) {
      throw std::runtime_error("Failed to create repository tarball");
    }

    const auto output_base = nano::runtime::starts_with(cli.output_dir, "/eos/") ? fs::path(cli.output_dir) : fs::absolute(cli.output_dir);
    const auto output_root = output_base / "pieces";
    fs::create_directories(output_root);

    const auto sample_map = nano::runtime::parse_sample_yaml(cli.input_yaml);
    std::map<std::string, std::vector<std::string>> files_by_nickname;
    for (const auto &[sample, datasets] : sample_map) {
      for (const auto &dataset : datasets) {
        const auto nickname = nickname_from_dataset(dataset, sample);
        std::cout << "Resolving sample=" << sample << " nickname=" << nickname << " dataset=" << dataset << "\n";
        const auto resolved = nano::runtime::resolve_dataset_entry(dataset);
        std::cout << "  files=" << resolved.size() << "\n";
        auto &files = files_by_nickname[nickname];
        files.insert(files.end(), resolved.begin(), resolved.end());
      }
    }

    std::vector<JobSpec> jobs;
    std::size_t job_index = 0;
    for (const auto &[nickname, files] : files_by_nickname) {
      std::cout << "Grouping nickname=" << nickname << " total_files=" << files.size() << "\n";
      std::size_t nickname_index = 0;
      for (std::size_t begin = 0; begin < files.size(); begin += cli.nfiles_per_job, ++nickname_index) {
        const auto end = std::min(files.size(), begin + cli.nfiles_per_job);
        JobSpec job;
        job.index = job_index++;
        job.nickname = nickname;
        job.nickname_index = nickname_index;
        job.inputs.assign(files.begin() + static_cast<std::ptrdiff_t>(begin), files.begin() + static_cast<std::ptrdiff_t>(end));
        job.output_file = (output_root / (safe_file_stem(nickname) + "_" + std::to_string(nickname_index) + ".root")).string();
        jobs.push_back(std::move(job));
      }
    }

    write_job_manifest(workdir / "job_manifest.json", jobs);
    std::ofstream index_list(workdir / "job_indices.txt");
    for (const auto &job : jobs) {
      index_list << job.index << "\n";
    }

    const auto variations_arg = cli.variations.empty() ? std::string("__none__") : cli.variations;
    const auto submit_jdl = render_template(
        template_dir / "submit.jdl.in",
        {
            {"@TREE_NAME@", cli.tree_name},
            {"@NUM_EVENTS@", std::to_string(cli.num_events)},
            {"@CHANNEL@", cli.channel},
            {"@VARIATIONS@", variations_arg},
        });
    write_text_file(workdir / "submit.jdl", submit_jdl);

    fs::create_directories(workdir / "logs");
    std::cout << "Created condor workdir: " << workdir << "\n";
    std::cout << "Output base dir: " << output_base << "\n";
    std::cout << "Piece output dir: " << output_root << "\n";
    std::cout << "Job manifest: " << (workdir / "job_manifest.json") << "\n";
    std::cout << "Job index list: " << (workdir / "job_indices.txt") << "\n";
    std::cout << "Jobs: " << jobs.size() << "\n";
    std::cout << "Next step:\n";
    std::cout << "  cd " << workdir << " && condor_submit submit.jdl\n";
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "nano_make_condor failed: " << ex.what() << "\n";
    return 1;
  }
}
