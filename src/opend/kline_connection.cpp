#include "opend/kline_connection.hpp"

#include "opend/connect_guard.hpp"

#include <thread>

namespace mmtrade {
namespace opend {

KlineConnection::KlineConnection() : connected_(false), get_ok_(false) {}

bool KlineConnection::Connect(const std::string &host, int port) {
  Disconnect();
  {
    OpenDConnectGuard guard;
    if (!InitQot(host.c_str(), static_cast<uint16_t>(port))) {
      last_error_ = "InitQot failed";
      return false;
    }
  }
  connected_ = true;
  MaybeSleepAfterOpenDConnect();
  return true;
}

void KlineConnection::Disconnect() {
  if (connected_) {
    UnInitQot();
    connected_ = false;
  }
}

Qot_Common::QotMarket KlineConnection::ToSdkMarket(const quote::QuoteSymbol &sym) const {
  if (sym.prefix == "HK") {
    return Qot_Common::QotMarket_HK_Security;
  }
  return Qot_Common::QotMarket_US_Security;
}

Qot_Common::KLType KlineConnection::ToSdkKlType(quote::KlTypeKey key) const {
  switch (key) {
    case quote::KlTypeKey::k1M:
      return Qot_Common::KLType_1Min;
    case quote::KlTypeKey::k3M:
      return Qot_Common::KLType_3Min;
    case quote::KlTypeKey::k5M:
      return Qot_Common::KLType_5Min;
    case quote::KlTypeKey::k15M:
      return Qot_Common::KLType_15Min;
    case quote::KlTypeKey::k30M:
      return Qot_Common::KLType_30Min;
    case quote::KlTypeKey::k60M:
      return Qot_Common::KLType_60Min;
    case quote::KlTypeKey::kDay:
      return Qot_Common::KLType_Day;
    case quote::KlTypeKey::kWeek:
      return Qot_Common::KLType_Week;
    case quote::KlTypeKey::kMon:
      return Qot_Common::KLType_Month;
    case quote::KlTypeKey::kQuarter:
      return Qot_Common::KLType_Quarter;
    case quote::KlTypeKey::kYear:
      return Qot_Common::KLType_Year;
  }
  return Qot_Common::KLType_5Min;
}

Qot_Common::SubType KlineConnection::ToSdkSubType(quote::KlTypeKey key) const {
  switch (key) {
    case quote::KlTypeKey::k1M:
      return Qot_Common::SubType_KL_1Min;
    case quote::KlTypeKey::k3M:
      return Qot_Common::SubType_KL_3Min;
    case quote::KlTypeKey::k5M:
      return Qot_Common::SubType_KL_5Min;
    case quote::KlTypeKey::k15M:
      return Qot_Common::SubType_KL_15Min;
    case quote::KlTypeKey::k30M:
      return Qot_Common::SubType_KL_30Min;
    case quote::KlTypeKey::k60M:
      return Qot_Common::SubType_KL_60Min;
    case quote::KlTypeKey::kDay:
      return Qot_Common::SubType_KL_Day;
    case quote::KlTypeKey::kWeek:
      return Qot_Common::SubType_KL_Week;
    case quote::KlTypeKey::kMon:
      return Qot_Common::SubType_KL_Month;
    case quote::KlTypeKey::kQuarter:
      return Qot_Common::SubType_KL_Qurater;
    case quote::KlTypeKey::kYear:
      return Qot_Common::SubType_KL_Year;
  }
  return Qot_Common::SubType_KL_5Min;
}

Qot_Common::RehabType KlineConnection::ToSdkRehab(quote::AuTypeKey key) const {
  switch (key) {
    case quote::AuTypeKey::kHfq:
      return Qot_Common::RehabType_Backward;
    case quote::AuTypeKey::kNone:
      return Qot_Common::RehabType_None;
    case quote::AuTypeKey::kQfq:
    default:
      return Qot_Common::RehabType_Forward;
  }
}

KlineFetchResult KlineConnection::Fetch(const std::string &code_full, quote::KlTypeKey kl_type,
                                        int req_num, bool extended_time, bool skip_subscribe) {
  KlineFetchResult out;
  if (!connected_) {
    out.error = "not connected";
    return out;
  }

  const quote::QuoteSymbol sym = quote::ParseQuoteSymbol(code_full);
  const Qot_Common::QotMarket market = ToSdkMarket(sym);
  const Qot_Common::KLType sdk_kl = ToSdkKlType(kl_type);
  const Qot_Common::SubType sdk_sub = ToSdkSubType(kl_type);

  if (!skip_subscribe) {
    Qot_Sub::Request sub_req;
    Qot_Sub::C2S *sub_c2s = sub_req.mutable_c2s();
    Qot_Common::Security *sec = sub_c2s->add_securitylist();
    sec->set_market(market);
    sec->set_code(sym.code);
    sub_c2s->add_subtypelist(sdk_sub);
    sub_c2s->set_issuborunsub(true);
    sub_c2s->set_isregorunregpush(false);
    sub_c2s->set_extendedtime(extended_time);
    WaitReply(m_pQotApi->Sub(sub_req));
  }

  get_ok_ = false;
  last_bars_.clear();
  Qot_GetKL::Request get_req;
  Qot_GetKL::C2S *get_c2s = get_req.mutable_c2s();
  get_c2s->set_rehabtype(Qot_Common::RehabType_Forward);
  get_c2s->set_kltype(sdk_kl);
  Qot_Common::Security *gsec = get_c2s->mutable_security();
  gsec->set_market(market);
  gsec->set_code(sym.code);
  get_c2s->set_reqnum(req_num);
  WaitReply(m_pQotApi->GetKL(get_req));

  out.ok = get_ok_;
  out.ret_code = out.ok ? 0 : -1;
  out.error = last_error_;
  out.bars = last_bars_;
  return out;
}

void KlineConnection::OnReply_Sub(Moomoo::u32_t, const Qot_Sub::Response &stRsp) {
  if (stRsp.rettype() != Common::RetType_Succeed) {
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

void KlineConnection::OnReply_GetKL(Moomoo::u32_t, const Qot_GetKL::Response &stRsp) {
  get_ok_ = (stRsp.rettype() == Common::RetType_Succeed);
  last_bars_.clear();
  if (get_ok_) {
    const Qot_GetKL::S2C &s2c = stRsp.s2c();
    for (int i = 0; i < s2c.kllist_size(); ++i) {
      const Qot_Common::KLine &kl = s2c.kllist(i);
      KlineBar bar;
      bar.time_key = kl.time();
      bar.timestamp_s = kl.timestamp();
      bar.open = kl.openprice();
      bar.high = kl.highprice();
      bar.low = kl.lowprice();
      bar.close = kl.closeprice();
      bar.volume = kl.volume();
      bar.turnover = kl.turnover();
      last_bars_.push_back(bar);
    }
  } else {
    last_error_ = stRsp.retmsg();
    PrintUTF8(stRsp.retmsg());
  }
  PostReply();
}

}  // namespace opend
}  // namespace mmtrade
