#pragma once

#include "opend/quote_snapshot.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mmtrade {
namespace opend {

// Per-symbol latest-wins handoff from the OpenD callback to the strategy loop.
// Publishing the same symbol before Drain overwrites the older unread book.
class BookMailbox {
 public:
  BookMailbox() : overwritten_(0) {}

  void Publish(const std::string &code, const QuoteSnapshot &snapshot) {
    if (code.empty() || !snapshot.valid) {
      return;
    }
    std::lock_guard<std::mutex> lk(mu_);
    if (pending_.find(code) != pending_.end()) {
      ++overwritten_;
    }
    pending_[code] = snapshot;
  }

  std::vector<std::pair<std::string, QuoteSnapshot> > Drain() {
    std::map<std::string, QuoteSnapshot> drained;
    {
      std::lock_guard<std::mutex> lk(mu_);
      drained.swap(pending_);
    }
    return std::vector<std::pair<std::string, QuoteSnapshot> >(drained.begin(), drained.end());
  }

  bool HasPending() const {
    std::lock_guard<std::mutex> lk(mu_);
    return !pending_.empty();
  }

  void Erase(const std::string &code) {
    std::lock_guard<std::mutex> lk(mu_);
    pending_.erase(code);
  }

  void Clear() {
    std::lock_guard<std::mutex> lk(mu_);
    pending_.clear();
  }

  std::uint64_t overwritten() const {
    std::lock_guard<std::mutex> lk(mu_);
    return overwritten_;
  }

 private:
  mutable std::mutex mu_;
  std::map<std::string, QuoteSnapshot> pending_;
  std::uint64_t overwritten_;
};

}  // namespace opend
}  // namespace mmtrade
