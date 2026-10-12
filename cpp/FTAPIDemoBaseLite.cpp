#include "FTAPIDemoBase.h"

FTAPIDemoBase::FTAPIDemoBase() {
  m_pQotApi = MMAPI::CreateQotApi();
  m_pQotApi->RegisterQotSpi(this);
  m_pQotApi->RegisterConnSpi(this);
  m_bQotInitSuc = false;

  m_pTrdApi = nullptr;
  m_bTrdInitSuc = false;
  m_pSem = new Semaphore(0);
}

FTAPIDemoBase::~FTAPIDemoBase() {
  UnInitQot();
  delete m_pSem;
}

bool FTAPIDemoBase::InitQot(const char *szIP, uint16_t nPort) {
  if (!m_bQotInitSuc && m_pQotApi != nullptr) {
    m_pQotApi->InitConnect(szIP, nPort, false);
    m_pSem->wait();
  }
  return m_bQotInitSuc;
}

void FTAPIDemoBase::UnInitQot() {
  if (m_pQotApi != nullptr) {
    m_pQotApi->UnregisterQotSpi();
    m_pQotApi->UnregisterConnSpi();
    MMAPI::ReleaseQotApi(m_pQotApi);
    m_pQotApi = nullptr;
    m_bQotInitSuc = false;
  }
}

bool FTAPIDemoBase::InitTrd(const char *, uint16_t, const char *, Trd_Common::SecurityFirm) {
  return false;
}

void FTAPIDemoBase::UnInitTrd() {}

void FTAPIDemoBase::Unlock(const char *, Trd_Common::SecurityFirm) {}

void FTAPIDemoBase::GetAccListAndSubTradePush() {}

void FTAPIDemoBase::OnInitConnect(MMAPI_Conn *pConn, Moomoo::i64_t nErrCode, const char *) {
  if (pConn == m_pQotApi) {
    m_bQotInitSuc = (nErrCode == 0);
  }
  PostReply();
}

void FTAPIDemoBase::OnReply_GetAccList(Moomoo::u32_t, const Trd_GetAccList::Response &) {
  PostReply();
}

void FTAPIDemoBase::OnReply_UnlockTrade(Moomoo::u32_t, const Trd_UnlockTrade::Response &) {
  PostReply();
}

void FTAPIDemoBase::OnReply_SubAccPush(Moomoo::u32_t, const Trd_SubAccPush::Response &) {
  PostReply();
}

void FTAPIDemoBase::WaitReply(int32_t nSerilNo) {
  if (nSerilNo != 0) {
    m_pSem->wait();
  }
}

void FTAPIDemoBase::PostReply() {
  m_pSem->post();
}

uint64_t FTAPIDemoBase::GetFirstAccID(Trd_Common::TrdMarket, Trd_Common::TrdEnv) {
  return 0;
}
