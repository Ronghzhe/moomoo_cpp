#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace mmtrade {
namespace trade {

struct OrderPushRow {
  std::string order_id;
  std::string order_status;
  std::string code;
  std::uint64_t numeric_order_id;
  double dealt_qty;
  double dealt_avg_price;
  std::int64_t create_ts_ms;
  std::int64_t fill_ts_ms;

  OrderPushRow()
      : numeric_order_id(0),
        dealt_qty(0.0),
        dealt_avg_price(0.0),
        create_ts_ms(0),
        fill_ts_ms(0) {}
};

// Thread-safe order push cache; mirrors Python order_push_hub.OrderPushHub.
class OrderPushCache {
 public:
  void Ingest(const OrderPushRow &row);
  void Erase(const std::string &order_id);
  bool GetRow(const std::string &order_id, OrderPushRow *out) const;
  std::uint64_t GetVersion(const std::string &order_id) const;
  double LastUpdateMono(const std::string &order_id) const;

  // Block until version > since_version or timeout. Returns false on timeout/no row.
  bool WaitUpdate(const std::string &order_id, std::uint64_t since_version, double timeout_sec,
                  OrderPushRow *out);

 private:
  static double NowMono();

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::unordered_map<std::string, OrderPushRow> rows_;
  std::unordered_map<std::string, std::uint64_t> versions_;
  std::unordered_map<std::string, double> last_mono_;
};

}  // namespace trade
}  // namespace mmtrade
