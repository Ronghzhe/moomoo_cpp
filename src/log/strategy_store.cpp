#include "log/strategy_store.hpp"

#include "log/ledger_ts.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

namespace mmtrade {
namespace log {

namespace {

std::string JoinPath(const std::string &a, const std::string &b) {
  if (a.empty()) {
    return b;
  }
  if (a[a.size() - 1] == '/') {
    return a + b;
  }
  return a + "/" + b;
}

bool MkDirRecursive(const std::string &path) {
  if (path.empty()) {
    return false;
  }
  std::string cur;
  for (size_t i = 0; i < path.size(); ++i) {
    cur.push_back(path[i]);
    if (path[i] == '/' || i + 1 == path.size()) {
      if (cur.size() <= 1) {
        continue;
      }
      if (mkdir(cur.c_str(), 0755) != 0) {
        // exists ok
      }
    }
  }
  return true;
}

std::string SymbolTag(const std::string &symbol) {
  std::string out;
  for (size_t i = 0; i < symbol.size(); ++i) {
    out.push_back(symbol[i] == '.' ? '_' : symbol[i]);
  }
  return out;
}

std::vector<std::string> SplitCsvLine(const std::string &line) {
  std::vector<std::string> out;
  std::string cur;
  bool in_quote = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (c == '"') {
      in_quote = !in_quote;
      continue;
    }
    if (c == ',' && !in_quote) {
      out.push_back(cur);
      cur.clear();
      continue;
    }
    cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

std::string CsvEscape(const std::string &s) {
  if (s.find(',') == std::string::npos && s.find('"') == std::string::npos &&
      s.find('\n') == std::string::npos) {
    return s;
  }
  std::ostringstream oss;
  oss << '"';
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '"') {
      oss << "\"\"";
    } else {
      oss << s[i];
    }
  }
  oss << '"';
  return oss.str();
}

std::string FormatPrice(double price) {
  std::ostringstream oss;
  oss.setf(std::ios::fixed);
  oss.precision(8);
  oss << price;
  std::string s = oss.str();
  while (!s.empty() && s[s.size() - 1] == '0') {
    s.erase(s.size() - 1, 1);
  }
  if (!s.empty() && s[s.size() - 1] == '.') {
    s.erase(s.size() - 1, 1);
  }
  return s;
}

std::string FormatQty(double qty) {
  const double r = static_cast<double>(static_cast<long long>(qty + (qty >= 0 ? 0.5 : -0.5)));
  if (std::abs(qty - r) < 1e-9) {
    std::ostringstream oss;
    oss << static_cast<long long>(r);
    return oss.str();
  }
  std::ostringstream oss;
  oss << qty;
  return oss.str();
}

int IndexOf(const std::vector<std::string> &header, const std::string &name) {
  for (size_t i = 0; i < header.size(); ++i) {
    if (header[i] == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void AppendCsvRow(const std::string &path, const std::vector<std::string> &cols) {
  std::ofstream out(path.c_str(), std::ios::app);
  for (size_t i = 0; i < cols.size(); ++i) {
    if (i > 0) {
      out << ',';
    }
    out << CsvEscape(cols[i]);
  }
  out << '\n';
}

}  // namespace

StrategyStore::StrategyStore(const std::string &csv_dir, const std::string &symbol,
                             std::int64_t acc_id, int acc_index)
    : symbol_(symbol) {
  std::ostringstream account;
  account << "acc_" << acc_id << "_idx_" << acc_index;
  csv_dir_path_ = JoinPath(JoinPath(csv_dir, account.str()), SymbolTag(symbol));
  MkDirRecursive(csv_dir_path_);
  events_file_ = JoinPath(csv_dir_path_, "events.csv");
  orders_file_ = JoinPath(csv_dir_path_, "orders.csv");
  runtime_file_ = JoinPath(csv_dir_path_, "runtime.log");
  runtime_logger_name_ = account.str() + "." + SymbolTag(symbol);
  EnsureHeaders();
}

void StrategyStore::EnsureHeaders() {
  if (!std::ifstream(events_file_.c_str()).good()) {
    AppendCsvRow(events_file_, {"ts", "level", "event_type", "message"});
  }
  if (!std::ifstream(orders_file_.c_str()).good()) {
    AppendCsvRow(orders_file_,
                 {"ts", "task_id", "symbol", "side", "order_id", "status", "qty", "price",
                  "remark", "budget", "est_notional", "sizing", "trd_env", "profit_status",
                  "profit_pct", "profit_amount"});
  }
}

void StrategyStore::LogRuntime(const std::string &level, const std::string &message) {
  std::ofstream out(runtime_file_.c_str(), std::ios::app);
  if (!out.good()) {
    return;
  }
  out << FormatDatetimeBeijing() << " | " << level << " | strategy_runtime." << runtime_logger_name_
      << " | " << message << '\n';
}

void StrategyStore::LogRuntimeLine(const std::string &level, const std::string &message) {
  LogRuntime(level, message);
}

void StrategyStore::LogRuntimeBlock(const std::string &level, const std::string &multiline_text) {
  size_t start = 0;
  while (start <= multiline_text.size()) {
    size_t end = multiline_text.find('\n', start);
    if (end == std::string::npos) {
      LogRuntime(level, multiline_text.substr(start));
      break;
    }
    LogRuntime(level, multiline_text.substr(start, end - start));
    start = end + 1;
  }
}

void StrategyStore::LogEvent(const std::string &level, const std::string &event_type,
                             const std::string &message) {
  AppendCsvRow(events_file_, {LedgerNowIso(symbol_), level, event_type, message});
  std::ostringstream msg;
  msg << event_type;
  if (!message.empty()) {
    msg << " | " << message;
  }
  LogRuntime(level, msg.str());
}

void StrategyStore::SaveOrder(const std::string &task_id, const std::string &symbol,
                              const std::string &side, const std::string &order_id,
                              const std::string &status, double qty, double price,
                              const std::string &remark, const std::string &budget,
                              const std::string &est_notional, const std::string &sizing,
                              const std::string &trd_env, const std::string &profit_status,
                              const std::string &profit_pct, const std::string &profit_amount) {
  AppendCsvRow(orders_file_,
               {LedgerNowIso(symbol), task_id, symbol, side, order_id, status, FormatQty(qty),
                FormatPrice(price), remark, budget, est_notional, sizing, trd_env, profit_status,
                profit_pct, profit_amount});
}

bool StrategyStore::LoadOrderRows(std::vector<std::string> *header,
                                  std::vector<std::vector<std::string> > *rows) {
  if (header == NULL || rows == NULL) {
    return false;
  }
  std::ifstream in(orders_file_.c_str());
  if (!in.good()) {
    return false;
  }
  std::string line;
  if (!std::getline(in, line)) {
    return false;
  }
  *header = SplitCsvLine(line);
  rows->clear();
  while (std::getline(in, line)) {
    if (!line.empty()) {
      rows->push_back(SplitCsvLine(line));
    }
  }
  return true;
}

bool StrategyStore::WriteOrderRows(const std::vector<std::string> &header,
                                    const std::vector<std::vector<std::string> > &rows) {
  std::ofstream out(orders_file_.c_str(), std::ios::trunc);
  if (!out.good()) {
    return false;
  }
  for (size_t i = 0; i < header.size(); ++i) {
    if (i > 0) {
      out << ',';
    }
    out << CsvEscape(header[i]);
  }
  out << '\n';
  for (size_t r = 0; r < rows.size(); ++r) {
    for (size_t c = 0; c < rows[r].size(); ++c) {
      if (c > 0) {
        out << ',';
      }
      out << CsvEscape(rows[r][c]);
    }
    out << '\n';
  }
  return true;
}

int StrategyStore::FindLastOrderRow(const std::vector<std::string> &header,
                                    const std::vector<std::vector<std::string> > &rows,
                                    const std::string &order_id) const {
  const int i_oid = IndexOf(header, "order_id");
  if (i_oid < 0) {
    return -1;
  }
  for (int i = static_cast<int>(rows.size()) - 1; i >= 0; --i) {
    if (static_cast<size_t>(i_oid) < rows[static_cast<size_t>(i)].size() &&
        rows[static_cast<size_t>(i)][static_cast<size_t>(i_oid)] == order_id) {
      return i;
    }
  }
  return -1;
}

bool StrategyStore::UpdateOrderFill(const std::string &order_id, const std::string &status,
                                    double price, double qty) {
  if (order_id.empty()) {
    return false;
  }
  std::vector<std::string> header;
  std::vector<std::vector<std::string> > rows;
  if (!LoadOrderRows(&header, &rows)) {
    return false;
  }
  const int hit = FindLastOrderRow(header, rows, order_id);
  if (hit < 0) {
    return false;
  }
  const int i_status = IndexOf(header, "status");
  const int i_price = IndexOf(header, "price");
  const int i_qty = IndexOf(header, "qty");
  std::vector<std::string> &row = rows[static_cast<size_t>(hit)];
  const size_t need = static_cast<size_t>(std::max(std::max(i_status, i_price), i_qty) + 1);
  if (row.size() < need) {
    row.resize(need);
  }
  if (i_status >= 0 && !status.empty()) {
    row[static_cast<size_t>(i_status)] = status;
  }
  if (i_price >= 0) {
    row[static_cast<size_t>(i_price)] = FormatPrice(price);
  }
  if (i_qty >= 0) {
    row[static_cast<size_t>(i_qty)] = FormatQty(qty);
  }
  return WriteOrderRows(header, rows);
}

bool StrategyStore::UpdateOrderProfit(const std::string &order_id, const std::string &trd_env,
                                      const std::string &profit_status,
                                      const std::string &profit_pct,
                                      const std::string &profit_amount) {
  if (order_id.empty()) {
    return false;
  }
  std::vector<std::string> header;
  std::vector<std::vector<std::string> > rows;
  if (!LoadOrderRows(&header, &rows)) {
    return false;
  }
  const int hit = FindLastOrderRow(header, rows, order_id);
  if (hit < 0) {
    return false;
  }
  const int i_env = IndexOf(header, "trd_env");
  const int i_st = IndexOf(header, "profit_status");
  const int i_pct = IndexOf(header, "profit_pct");
  const int i_amt = IndexOf(header, "profit_amount");
  std::vector<std::string> &row = rows[static_cast<size_t>(hit)];
  const size_t need =
      static_cast<size_t>(std::max(std::max(i_env, i_st), std::max(i_pct, i_amt)) + 1);
  if (row.size() < need) {
    row.resize(need);
  }
  if (i_env >= 0) {
    row[static_cast<size_t>(i_env)] = trd_env;
  }
  if (i_st >= 0) {
    row[static_cast<size_t>(i_st)] = profit_status;
  }
  if (i_pct >= 0) {
    row[static_cast<size_t>(i_pct)] = profit_pct;
  }
  if (i_amt >= 0) {
    row[static_cast<size_t>(i_amt)] = profit_amount;
  }
  return WriteOrderRows(header, rows);
}

}  // namespace log
}  // namespace mmtrade
