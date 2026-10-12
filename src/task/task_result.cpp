#include "task/task_result.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace mmtrade {
namespace task {

namespace {

std::string ToLower(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

std::string Trim(const std::string &s) {
  size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) {
    ++b;
  }
  size_t e = s.size();
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) {
    --e;
  }
  return s.substr(b, e - b);
}

struct ErrSpec {
  const char *message;
  const char *detail;
};

// Mirrors strategy/task_system/task_result.py ERROR_CATALOG (+ SHORT_SELL variants).
const ErrSpec *CatalogSpec(const std::string &code) {
  static const struct {
    const char *code;
    ErrSpec spec;
  } kCatalog[] = {
      {"HK_SHORT_ENTRY_UNFILLED", {"开空时重试五次未成交", "港股开空开仓五次重试后仍未成交"}},
      {"ENTRY_QTY_BELOW_LOT", {"开仓数量不足最低手数", "开仓数量低于最低手数"}},
      {"ENTRY_BUDGET_BELOW_MIN_LOT", {"达不到最低报单金额", "预算不足以购买一手"}},
      {"ENTRY_PLACE_REJECTED", {"开仓报单被拒", "开仓报单被券商或交易所拒绝"}},
      {"EXIT_PLACE_REJECTED", {"平仓报单被拒", "平仓报单被券商或交易所拒绝"}},
      {"ENTRY_ORDER_FAILED", {"开仓失败", "开仓订单执行失败"}},
      {"EXIT_ORDER_FAILED", {"平仓失败", "平仓订单执行失败"}},
      {"ENTRY_RETRY_EXCEEDED", {"开仓重试次数已用尽", "开仓重试次数已用尽"}},
      {"EXIT_RETRY_EXCEEDED", {"平仓重试次数已用尽", "平仓重试次数已用尽"}},
      {"TRADING_FATAL", {"盘中交易异常终止", "盘中交易流程异常终止"}},
      {"UNLOCK_RATE_LIMIT",
       {"实盘解锁限频", "Unlock 接口 30 秒内超过 10 次，已安排自动重试"}},
      {"SHORT_POOL_INSUFFICIENT",
       {"做空池不足", "Insufficient short selling quota available"}},
      {"SHORT_SELL_PRICE_BELOW_ASK",
       {"做空卖价低于卖一", "The short selling order price cannot be lower than Ask 1"}},
      {"ORDER_PRICE_LIMIT_EXCEEDED",
       {"委托价超出交易所输入价限制", "order input price limit exceeded"}},
      {"INVALID_ORDER_PRICE",
       {"委托价格不合法", "Invalid price. Please enter the correct price"}},
      {"INVALID_ORDER_QUANTITY", {"委托数量不合法", "Invalid quantity."}},
      {"INSUFFICIENT_POSITION", {"持仓不足", "Not enough positions"}},
      {"INSUFFICIENT_BUYING_POWER",
       {"账户购买力不足",
        "Insufficient buying power. You can increase it by exchanging currencies, "
        "depositing funds, or using other methods."}},
      {"ACCOUNT_ERROR", {"账户配置错误", "账户参数或权限配置有误"}},
      {"VALIDATION_FAILED", {"任务参数校验或映射失败", "任务参数校验或映射失败"}},
      {"TIMEOUT", {"任务执行超时", "策略子进程执行超时"}},
      {"RUNTIME_ERROR", {"策略运行错误", "策略运行过程中发生未分类错误"}},
      {"UNKNOWN", {"未知错误", "未知错误类型"}},
  };
  for (size_t i = 0; i < sizeof(kCatalog) / sizeof(kCatalog[0]); ++i) {
    if (code == kCatalog[i].code) {
      return &kCatalog[i].spec;
    }
  }
  return NULL;
}

const ErrSpec *ShortSellVariantSpec(const std::string &variant) {
  static const ErrSpec kOrderType = {"订单类型不支持做空",
                                    "The order type does not support short selling"};
  static const ErrSpec kNotSupported = {"当前不支持做空",
                                       "Short selling is currently not supported"};
  static const ErrSpec kProduct = {"标的不支持做空",
                                   "The product is not eligible for short selling"};
  if (variant == "order_type") {
    return &kOrderType;
  }
  if (variant == "product_ineligible") {
    return &kProduct;
  }
  return &kNotSupported;
}

std::string ShortSellVariantFromHint(const std::string &hint) {
  const std::string t = ToLower(Trim(hint));
  if (t.empty()) {
    return "";
  }
  if (t.find("order type does not support short selling") != std::string::npos) {
    return "order_type";
  }
  if (t.find("short selling is currently not supported") != std::string::npos) {
    return "not_supported";
  }
  if (t.find("product is not eligible for short selling") != std::string::npos) {
    return "product_ineligible";
  }
  return "";
}

std::string LegacyShortSellVariant(const std::string &legacy_code) {
  if (legacy_code == "SHORT_ORDER_TYPE_UNSUPPORTED") {
    return "order_type";
  }
  if (legacy_code == "SHORT_SELL_NOT_SUPPORTED") {
    return "not_supported";
  }
  if (legacy_code == "SHORT_PRODUCT_INELIGIBLE") {
    return "product_ineligible";
  }
  return "";
}

bool IsOrderRateLimitErr(const std::string &detail) {
  const std::string t = ToLower(detail);
  if (t.empty()) {
    return false;
  }
  return t.find("high frequency") != std::string::npos ||
         t.find("maximum 10 times per 30 seconds") != std::string::npos ||
         t.find("maximum 10 times/30 seconds") != std::string::npos ||
         t.find("maximum 15 times per 30 seconds") != std::string::npos ||
         t.find("maximum 15 times/30 seconds") != std::string::npos ||
         t.find("too many requests") != std::string::npos ||
         t.find("rate limit") != std::string::npos || detail.find("频率") != std::string::npos ||
         detail.find("限频") != std::string::npos;
}

std::string BrokerErrorCodeFromHint(const std::string &hint) {
  const std::string t = ToLower(Trim(hint));
  if (t.empty()) {
    return "";
  }
  if (!ShortSellVariantFromHint(t).empty()) {
    return "SHORT_SELL_NOT_SUPPORTED";
  }
  if (t.find("insufficient short selling quota") != std::string::npos ||
      (t.find("short selling quota") != std::string::npos &&
       t.find("insufficient") != std::string::npos)) {
    return "SHORT_POOL_INSUFFICIENT";
  }
  if (t.find("short selling order price cannot be lower than ask") != std::string::npos) {
    return "SHORT_SELL_PRICE_BELOW_ASK";
  }
  if (t.find("order input price limit") != std::string::npos ||
      t.find("input price limit") != std::string::npos) {
    return "ORDER_PRICE_LIMIT_EXCEEDED";
  }
  if (t.find("invalid price") != std::string::npos &&
      t.find("please enter the correct price") != std::string::npos) {
    return "INVALID_ORDER_PRICE";
  }
  if (t.find("invalid quantity") != std::string::npos) {
    return "INVALID_ORDER_QUANTITY";
  }
  if (t.find("insufficient buying power") != std::string::npos ||
      hint.find("购买力不足") != std::string::npos) {
    return "INSUFFICIENT_BUYING_POWER";
  }
  if (t.find("not enough positions") != std::string::npos ||
      t.find("insufficient position") != std::string::npos) {
    return "INSUFFICIENT_POSITION";
  }
  return "";
}

// Prefer broker/run_outcome hint for order fatals so DB detail is the real reason,
// not only the catalog paraphrase (e.g. "平仓订单执行失败").
bool PassThroughDetailCode(const std::string &code) {
  return code == "RUNTIME_ERROR" || code == "UNKNOWN" || code == "TRADING_FATAL" ||
         code == "UNLOCK_RATE_LIMIT" || code == "ENTRY_PLACE_REJECTED" ||
         code == "EXIT_PLACE_REJECTED" || code == "ENTRY_ORDER_FAILED" ||
         code == "EXIT_ORDER_FAILED" || code == "ENTRY_RETRY_EXCEEDED" ||
         code == "EXIT_RETRY_EXCEEDED" || code == "INVALID_ORDER_QUANTITY" ||
         code == "INVALID_ORDER_PRICE" || code == "ORDER_PRICE_LIMIT_EXCEEDED" ||
         code == "INSUFFICIENT_POSITION" || code == "INSUFFICIENT_BUYING_POWER" ||
         code == "SHORT_POOL_INSUFFICIENT" || code == "SHORT_SELL_PRICE_BELOW_ASK";
}

std::string DetailWithRuntimeHint(const std::string &code, const std::string &catalog_detail,
                                  const std::string &hint) {
  const std::string compact = Trim(hint);
  if (compact.empty()) {
    return catalog_detail;
  }
  if (!PassThroughDetailCode(code)) {
    return catalog_detail;
  }
  const size_t max_len = (code == "UNLOCK_RATE_LIMIT") ? 480 : 2000;
  return compact.size() > max_len ? compact.substr(0, max_len) : compact;
}

nlohmann::json EmptyLeg() { return nlohmann::json{{"qty", nullptr}, {"avg_price", nullptr}}; }

nlohmann::json ErrObj(const std::string &code, const std::string &message,
                      const std::string &detail) {
  return nlohmann::json{{"code", code}, {"message", message}, {"detail", detail}};
}

nlohmann::json ResolveShortSellNotSupported(const std::string &hint,
                                            const std::string &legacy_code) {
  std::string variant = ShortSellVariantFromHint(hint);
  if (variant.empty() && !legacy_code.empty()) {
    variant = LegacyShortSellVariant(legacy_code);
  }
  if (variant.empty()) {
    variant = "not_supported";
  }
  const ErrSpec *spec = ShortSellVariantSpec(variant);
  return ErrObj("SHORT_SELL_NOT_SUPPORTED", spec->message, spec->detail);
}

// Mirrors Python resolve_task_error → (code, message, detail).
nlohmann::json ResolveTaskError(const std::string &code, const std::string &hint) {
  const std::string compact_hint = Trim(hint);
  const std::string fatal_code = Trim(code);

  if (!ShortSellVariantFromHint(compact_hint).empty()) {
    return ResolveShortSellNotSupported(compact_hint, "");
  }
  if (!LegacyShortSellVariant(fatal_code).empty()) {
    return ResolveShortSellNotSupported(compact_hint, fatal_code);
  }

  const std::string broker_code = BrokerErrorCodeFromHint(compact_hint);
  if (!broker_code.empty()) {
    if (broker_code == "SHORT_SELL_NOT_SUPPORTED") {
      return ResolveShortSellNotSupported(compact_hint, "");
    }
    const ErrSpec *spec = CatalogSpec(broker_code);
    if (spec != NULL) {
      return ErrObj(broker_code, spec->message,
                   DetailWithRuntimeHint(broker_code, spec->detail, compact_hint));
    }
  }

  if (fatal_code == "HK_SHORT_ENTRY_UNFILLED" || fatal_code == "HK_SHORT_PRICE_OUT_OF_BAND") {
    // Unified HK short entry fatal (out-of-band uses dedicated message).
    if (fatal_code == "HK_SHORT_PRICE_OUT_OF_BAND" ||
        ToLower(compact_hint).find("out of band") != std::string::npos ||
        compact_hint.find("超出") != std::string::npos) {
      return ErrObj("HK_SHORT_ENTRY_UNFILLED", "开空时价格超出允许区间",
                   compact_hint.empty() ? "港股开空开仓价格超出允许区间" : compact_hint);
    }
    const ErrSpec *spec = CatalogSpec("HK_SHORT_ENTRY_UNFILLED");
    return ErrObj("HK_SHORT_ENTRY_UNFILLED", spec->message, spec->detail);
  }

  if (fatal_code == "UNLOCK_RATE_LIMIT" ||
      (ToLower(compact_hint).find("unlock trading") != std::string::npos &&
       IsOrderRateLimitErr(compact_hint))) {
    const ErrSpec *spec = CatalogSpec("UNLOCK_RATE_LIMIT");
    return ErrObj("UNLOCK_RATE_LIMIT", spec->message,
                 DetailWithRuntimeHint("UNLOCK_RATE_LIMIT", spec->detail, compact_hint));
  }

  const ErrSpec *known = CatalogSpec(fatal_code);
  if (known != NULL) {
    return ErrObj(fatal_code, known->message,
                 DetailWithRuntimeHint(fatal_code, known->detail, compact_hint));
  }

  const ErrSpec *runtime = CatalogSpec("RUNTIME_ERROR");
  return ErrObj("RUNTIME_ERROR", runtime->message,
               DetailWithRuntimeHint("RUNTIME_ERROR", runtime->detail, compact_hint));
}

}  // namespace

nlohmann::json BuildFTaskResult(const nlohmann::json *entry_result, const nlohmann::json *exit_result,
                                const nlohmann::json *error) {
  nlohmann::json e = EmptyLeg();
  nlohmann::json x = EmptyLeg();
  if (entry_result != NULL && entry_result->is_object()) {
    if (entry_result->contains("qty")) {
      e["qty"] = (*entry_result)["qty"];
    }
    if (entry_result->contains("avg_price")) {
      e["avg_price"] = (*entry_result)["avg_price"];
    }
  }
  if (exit_result != NULL && exit_result->is_object()) {
    if (exit_result->contains("qty")) {
      x["qty"] = (*exit_result)["qty"];
    }
    if (exit_result->contains("avg_price")) {
      x["avg_price"] = (*exit_result)["avg_price"];
    }
  }
  nlohmann::json out = {{"entry_result", e},
                        {"exit_result", x},
                        {"error", error == NULL ? nullptr : *error}};
  return out;
}

nlohmann::json ErrorForCode(const std::string &code, const std::string &hint) {
  return ResolveTaskError(code, hint);
}

nlohmann::json ComposeFTaskResultFromWorker(const nlohmann::json &run_result) {
  if (run_result.contains("f_task_result") && run_result.at("f_task_result").is_object()) {
    const nlohmann::json &ft = run_result.at("f_task_result");
    if (ft.contains("entry_result") && ft.at("entry_result").is_object() &&
        ft.contains("exit_result") && ft.at("exit_result").is_object()) {
      nlohmann::json normalized_err = nullptr;
      if (ft.contains("error") && !ft.at("error").is_null()) {
        if (ft.at("error").is_object()) {
          const std::string ec = Trim(ft.at("error").value("code", ""));
          const std::string eh = Trim(ft.at("error").value("detail", ""));
          normalized_err = ResolveTaskError(ec, eh);
        } else {
          const ErrSpec *spec = CatalogSpec("UNKNOWN");
          normalized_err = ErrObj("UNKNOWN", spec->message, spec->detail);
        }
      }
      const nlohmann::json er = ft.at("entry_result");
      const nlohmann::json xr = ft.at("exit_result");
      if (normalized_err.is_null()) {
        return BuildFTaskResult(&er, &xr, NULL);
      }
      return BuildFTaskResult(&er, &xr, &normalized_err);
    }
  }

  const bool ok = run_result.value("ok", false);
  const int exit_code = run_result.value("exit_code", 1);
  const std::string raw_err = Trim(run_result.value("error", ""));
  const std::string note = run_result.value("note", "");
  const nlohmann::json run_outcome =
      run_result.contains("run_outcome") ? run_result.at("run_outcome") : nlohmann::json();

  if (exit_code == 2 || (run_outcome.is_object() && run_outcome.value("fatal", false))) {
    const std::string err_code =
        run_outcome.is_object() ? Trim(run_outcome.value("code", "TRADING_FATAL"))
                                : "TRADING_FATAL";
    const std::string outcome_hint =
        run_outcome.is_object() ? Trim(run_outcome.value("detail", "")) : "";
    const std::string hint = !outcome_hint.empty() ? outcome_hint : raw_err;
    const nlohmann::json err = ResolveTaskError(err_code, hint);
    return BuildFTaskResult(NULL, NULL, &err);
  }
  if (ok && (exit_code == 0 || run_result.value("simulate", false))) {
    return BuildFTaskResult(NULL, NULL, NULL);
  }
  if (ToLower(note).find("account configuration error") != std::string::npos ||
      raw_err.find("账户错误") == 0) {
    const nlohmann::json err = ResolveTaskError("ACCOUNT_ERROR", raw_err.empty() ? note : raw_err);
    return BuildFTaskResult(NULL, NULL, &err);
  }
  if (note.find("validation/mapping") != std::string::npos ||
      ToLower(note).find("mapping failed") != std::string::npos) {
    const nlohmann::json err = ResolveTaskError("VALIDATION_FAILED", note);
    return BuildFTaskResult(NULL, NULL, &err);
  }
  if (exit_code == 124 || ToLower(raw_err).find("timeout") != std::string::npos) {
    const nlohmann::json err = ResolveTaskError("TIMEOUT", raw_err);
    return BuildFTaskResult(NULL, NULL, &err);
  }
  const nlohmann::json err = ResolveTaskError("RUNTIME_ERROR", raw_err.empty() ? note : raw_err);
  return BuildFTaskResult(NULL, NULL, &err);
}

}  // namespace task
}  // namespace mmtrade
