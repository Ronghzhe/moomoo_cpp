#include "task/task_params_spec.hpp"

namespace mmtrade {
namespace task {

namespace {

bool IsNumber(const nlohmann::json &v) {
  return v.is_number() && !v.is_boolean();
}

void RequireKeys(const nlohmann::json &payload, const char *keys[], size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (!payload.contains(keys[i])) {
      throw std::runtime_error(std::string("f_task_params missing required keys: ") + keys[i]);
    }
  }
}

}  // namespace

void ValidateTaskParams(const nlohmann::json &payload) {
  static const char *kRequired[] = {
      "task_time", "side", "order_type", "price", "task_type", "stock_type", "stock_code",
      "entry_conditions", "exit_conditions", "trade_qty", "trade_amount", "note",
  };
  RequireKeys(payload, kRequired, sizeof(kRequired) / sizeof(kRequired[0]));

  if (!IsNumber(payload.at("task_time"))) {
    throw std::runtime_error("task_time must be timestamp number");
  }

  const std::string side = payload.at("side").get<std::string>();
  if (side != "long" && side != "short") {
    throw std::runtime_error("side must be one of long, short");
  }

  const std::string order_type = payload.at("order_type").get<std::string>();
  if (order_type != "market" && order_type != "limit") {
    throw std::runtime_error("order_type must be one of limit, market");
  }
  if (order_type == "limit" && payload.at("price").is_null()) {
    throw std::runtime_error("price is required when order_type=limit");
  }

  const nlohmann::json &raw_tt = payload.at("task_type");
  if (raw_tt.is_null()) {
    throw std::runtime_error("task_type is required and must be non-empty");
  }
  const std::string task_type = raw_tt.get<std::string>();
  if (task_type.empty()) {
    throw std::runtime_error("task_type is required and must be non-empty");
  }
  static const char *kAllowedTaskType[] = {
      "monitor_entry", "monitor_exit", "monitor_all", "entry_now", "exit_now",
  };
  bool ok_tt = false;
  for (size_t i = 0; i < sizeof(kAllowedTaskType) / sizeof(kAllowedTaskType[0]); ++i) {
    if (task_type == kAllowedTaskType[i]) {
      ok_tt = true;
      break;
    }
  }
  if (!ok_tt) {
    throw std::runtime_error("task_type must be one of allowed monitor/entry/exit types");
  }

  const std::string stock_type = payload.at("stock_type").get<std::string>();
  if (stock_type != "US" && stock_type != "HK") {
    throw std::runtime_error("stock_type must be one of HK, US");
  }

  const std::string stock_code = payload.at("stock_code").get<std::string>();
  if (stock_code.empty()) {
    throw std::runtime_error("stock_code is required");
  }

  for (const char *field : {"entry_conditions", "exit_conditions"}) {
    const nlohmann::json &conds = payload.at(field);
    if (!conds.is_array()) {
      throw std::runtime_error(std::string(field) + " must be list");
    }
    for (size_t i = 0; i < conds.size(); ++i) {
      const nlohmann::json &c = conds.at(i);
      if (!c.is_object()) {
        throw std::runtime_error(std::string(field) + "[" + std::to_string(i) + "] must be object");
      }
      if (!c.contains("s_name") || !c.contains("params")) {
        throw std::runtime_error(std::string(field) + "[" + std::to_string(i) +
                                 "] must contain s_name and params");
      }
      if (!c.at("params").is_object()) {
        throw std::runtime_error(std::string(field) + "[" + std::to_string(i) + "].params must be object");
      }
    }
  }

  const nlohmann::json &qty = payload.at("trade_qty");
  const nlohmann::json &amount = payload.at("trade_amount");
  if (!qty.is_null() && !amount.is_null()) {
    throw std::runtime_error("trade_qty and trade_amount are mutually exclusive");
  }
  if (!qty.is_null() && !IsNumber(qty)) {
    throw std::runtime_error("trade_qty must be number or null");
  }
  if (!amount.is_null() && !IsNumber(amount)) {
    throw std::runtime_error("trade_amount must be number or null");
  }
}

}  // namespace task
}  // namespace mmtrade
