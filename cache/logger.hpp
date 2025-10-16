#pragma once
#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <filesystem>

class CsvLogger {
public:
  static CsvLogger& instance() { static CsvLogger L; return L; }

  void init(const std::string& path = "../data/access_log.csv") {
    std::lock_guard<std::mutex> lock(mu_);
    path_ = path;
    std::filesystem::create_directories(std::filesystem::path(path_).parent_path());
    // create file with header if new
    if (!std::filesystem::exists(path_)) {
      std::ofstream f(path_, std::ios::out);
      f << "ts_ms,op,key,hit,lat_us,size_bytes\n";
    }
  }

  void write(const std::string& op, const std::string& key,
             bool hit, uint64_t lat_us, size_t size_bytes) {
    std::lock_guard<std::mutex> lock(mu_);
    std::ofstream f(path_, std::ios::app);
    f << now_ms() << ',' << op << ',' << escape(key) << ','
      << (hit ? 1 : 0) << ',' << lat_us << ',' << size_bytes << '\n';
  }

private:
  std::mutex mu_;
  std::string path_ = "../data/access_log.csv";

  static uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  }

  static std::string escape(const std::string& s) {
    // keep simple for now (no commas in keys)
    return s;
  }
};
