#include "task/pipeline_latency.hpp"
#include "task/task_latency_recorder.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

std::string ReadAll(const std::string &path) {
  std::ifstream ifs(path.c_str());
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST(PipelineLatency, ResetAllowsSecondTaskToRecord) {
  const std::string staging = "/tmp/moomoo_pipeline_latency_test.json";
  ::unlink(staging.c_str());

  mmtrade::task::TaskLatencyRecorder &rec = mmtrade::task::TaskLatencyRecorder::Instance();
  rec.Configure(staging, "");
  mmtrade::task::PipelineLatency::OnMdSdkRecv();
  rec.FlushAndWait();
  EXPECT_NE(ReadAll(staging).find("md_sdk_recv_ms"), std::string::npos);

  ::unlink(staging.c_str());
  rec.Configure(staging, "");
  rec.FlushAndWait();
  EXPECT_EQ(ReadAll(staging).find("md_sdk_recv_ms"), std::string::npos);

  mmtrade::task::PipelineLatency::OnMdSdkRecv();
  rec.FlushAndWait();
  EXPECT_NE(ReadAll(staging).find("md_sdk_recv_ms"), std::string::npos);

  rec.Configure("", "");
  ::unlink(staging.c_str());
}

TEST(PipelineLatency, DisabledReturnsBeforeRecording) {
  const std::string staging = "/tmp/moomoo_pipeline_latency_disabled.json";
  ::unlink(staging.c_str());
  mmtrade::task::TaskLatencyRecorder &rec = mmtrade::task::TaskLatencyRecorder::Instance();
  rec.Configure("", "");
  mmtrade::task::PipelineLatency::Reset();
  for (int i = 0; i < 1000; ++i) {
    mmtrade::task::PipelineLatency::OnMdSdkRecv();
    mmtrade::task::PipelineLatency::OnMdProtoDecoded();
    mmtrade::task::PipelineLatency::OnMdCallbackNotified();
    mmtrade::task::PipelineLatency::OnMdLoopWakeup();
  }
  rec.FlushAndWait();
  EXPECT_FALSE(::access(staging.c_str(), F_OK) == 0);
}

TEST(PipelineLatency, TwoThreadsMergeIntoSameTask) {
  const std::string staging = "/tmp/moomoo_pipeline_latency_mt.json";
  ::unlink(staging.c_str());
  mmtrade::task::TaskLatencyRecorder &rec = mmtrade::task::TaskLatencyRecorder::Instance();
  rec.Configure(staging, "");

  std::thread md([]() {
    mmtrade::task::PipelineLatency::OnMdSdkRecv();
    mmtrade::task::PipelineLatency::OnMdProtoDecoded();
    mmtrade::task::PipelineLatency::OnMdCallbackNotified();
  });
  std::thread st([]() {
    mmtrade::task::PipelineLatency::OnMdLoopWakeup();
    mmtrade::task::PipelineLatency::OnEntryStrategyDone();
  });
  md.join();
  st.join();
  rec.FlushAndWait();

  const std::string body = ReadAll(staging);
  EXPECT_NE(body.find("md_sdk_recv_ms"), std::string::npos);
  EXPECT_NE(body.find("md_proto_done_ms"), std::string::npos);
  EXPECT_NE(body.find("md_callback_ms"), std::string::npos);
  EXPECT_NE(body.find("md_loop_wakeup_ms"), std::string::npos);
  EXPECT_NE(body.find("entry_pipe_strategy_ms"), std::string::npos);

  rec.Configure("", "");
  ::unlink(staging.c_str());
}
