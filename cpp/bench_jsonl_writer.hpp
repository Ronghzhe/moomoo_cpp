#pragma once

#include <fstream>
#include <iostream>
#include <string>

// Bench JSONL：进程内单文件句柄，每行写入后 flush（长跑/崩溃时尽量不丢已写行）。
class BenchJsonlWriter {
 public:
  explicit BenchJsonlWriter(const std::string &path) : path_(path), warned_(false) {
    ofs_.open(path_.c_str(), std::ios::out | std::ios::app);
    if (!ofs_.good()) {
      WarnOpenFailed();
    }
  }

  ~BenchJsonlWriter() {
    if (ofs_.is_open()) {
      ofs_.flush();
      ofs_.close();
    }
  }

  BenchJsonlWriter(const BenchJsonlWriter &) = delete;
  BenchJsonlWriter &operator=(const BenchJsonlWriter &) = delete;

  bool good() const { return ofs_.is_open() && ofs_.good(); }

  void AppendLine(const std::string &line) {
    if (!good()) {
      WarnOpenFailed();
      return;
    }
    ofs_ << line << '\n';
    ofs_.flush();
    if (!ofs_.good()) {
      WarnOpenFailed();
    }
  }

 private:
  void WarnOpenFailed() {
    if (warned_) {
      return;
    }
    warned_ = true;
    std::cerr << "failed to write jsonl: " << path_ << std::endl;
  }

  std::string path_;
  std::ofstream ofs_;
  bool warned_;
};
