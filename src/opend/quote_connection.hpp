#pragma once

#include "opend/kline_connection.hpp"
#include "opend/quote_snapshot.hpp"
#include "opend/trade_connection.hpp"

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace mmtrade {
namespace opend {

// Shares one FTAPIDemoBase (InitQot + InitTrd on the same object). Required for strategy_cpp.
class QuoteConnection : public TradeConnection {
 public:
  QuoteConnection();

  bool Connect(const std::string &host, int port);
  void Disconnect();
  void DisconnectTrade();

  bool SubscribeOrderBook(const std::vector<std::string> &codes_full);
  bool UnsubscribeOrderBook(const std::vector<std::string> &codes_full);
  void ClearQuoteCache();
  bool quote_connected() const { return connected_; }
  bool RefreshOrderBook(const std::string &code_full);
  bool RefreshMarketSnapshot(const std::string &code_full);
  double FetchDayOpenPrice(const std::string &code_full);

  QuoteSnapshot GetSnapshot(const std::string &code_full) const;

  // Sample codes (US./HK.); fills per-code and per-market (US/HK) phase: pre/rth/post/overnight.
  bool RefreshMarketState(const std::vector<std::string> &codes_full);
  std::string MarketPhaseForCode(const std::string &code_full) const;
  std::string MarketPhaseForTag(const std::string &market_tag) const;

  // Fetch historical klines on the existing InitQot connection (no reconnect).
  KlineFetchResult FetchHistoricalKlines(const std::string &code_full,
                                       quote::KlTypeKey kl_type, int req_num,
                                       bool extended_time = true);

  // Block until orderbook push or timeout (seconds). Returns true if a push arrived.
  bool WaitBookPush(double timeout_sec);

 private:
  void NotifyBookPush();
  Qot_Common::QotMarket ToSdkMarket(const std::string &code_full) const;
  std::string SdkCode(const std::string &code_full) const;

  void OnReply_Sub(Moomoo::u32_t, const Qot_Sub::Response &stRsp) override;
  void OnReply_GetOrderBook(Moomoo::u32_t, const Qot_GetOrderBook::Response &stRsp) override;
  void OnReply_GetSecuritySnapshot(Moomoo::u32_t,
                                   const Qot_GetSecuritySnapshot::Response &stRsp) override;
  void OnReply_GetKL(Moomoo::u32_t, const Qot_GetKL::Response &stRsp) override;
  void OnReply_GetMarketState(Moomoo::u32_t,
                              const Qot_GetMarketState::Response &stRsp) override;
  void OnPush_UpdateOrderBook(const Qot_UpdateOrderBook::Response &stRsp) override;

  Qot_Common::QotMarket ToSdkMarketSymbol(const quote::QuoteSymbol &sym) const;
  Qot_Common::KLType ToSdkKlType(quote::KlTypeKey key) const;
  Qot_Common::SubType ToSdkSubType(quote::KlTypeKey key) const;

  bool connected_;
  bool sub_ok_;
  bool get_ok_;
  bool snap_ok_;
  double snap_open_;
  int snap_lot_size_;
  double snap_last_price_;
  int snap_sec_status_;
  bool snap_suspended_;
  mutable std::mutex mu_;
  std::map<std::string, QuoteSnapshot> cache_;
  std::mutex book_wait_mu_;
  std::condition_variable book_wait_cv_;
  std::uint64_t book_push_seq_;
  bool kline_get_ok_;
  std::vector<KlineBar> kline_last_bars_;
  std::string kline_last_error_;
  bool market_state_ok_;
  std::map<std::string, std::string> market_phase_by_code_;
  std::map<std::string, std::string> market_phase_by_tag_;
};

}  // namespace opend
}  // namespace mmtrade
