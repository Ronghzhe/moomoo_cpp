#include "opend/book_mailbox.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

mmtrade::opend::QuoteSnapshot Book(double bid, double ask, std::int64_t ts_ms) {
  mmtrade::opend::QuoteSnapshot out;
  out.bid_1 = bid;
  out.ask_1 = ask;
  out.bid_count = 1;
  out.ask_count = 1;
  out.bid_prices[0] = bid;
  out.ask_prices[0] = ask;
  out.valid = true;
  out.local_ts_ms = ts_ms;
  return out;
}

}  // namespace

TEST(BookMailboxTest, LatestUnreadBookWinsPerSymbol) {
  mmtrade::opend::BookMailbox mailbox;
  mailbox.Publish("US.AAPL", Book(100.0, 100.1, 1));
  mailbox.Publish("US.AAPL", Book(101.0, 101.1, 2));

  const std::vector<std::pair<std::string, mmtrade::opend::QuoteSnapshot> > drained =
      mailbox.Drain();
  ASSERT_EQ(drained.size(), 1u);
  EXPECT_EQ(drained[0].first, "US.AAPL");
  EXPECT_DOUBLE_EQ(drained[0].second.bid_1, 101.0);
  EXPECT_EQ(drained[0].second.local_ts_ms, 2);
  EXPECT_EQ(mailbox.overwritten(), 1u);
  EXPECT_FALSE(mailbox.HasPending());
}

TEST(BookMailboxTest, DrainReturnsOneLatestBookPerSymbol) {
  mmtrade::opend::BookMailbox mailbox;
  mailbox.Publish("US.MSFT", Book(200.0, 200.1, 1));
  mailbox.Publish("US.AAPL", Book(100.0, 100.1, 2));

  const std::vector<std::pair<std::string, mmtrade::opend::QuoteSnapshot> > drained =
      mailbox.Drain();
  ASSERT_EQ(drained.size(), 2u);
  EXPECT_EQ(drained[0].first, "US.AAPL");
  EXPECT_EQ(drained[1].first, "US.MSFT");
  EXPECT_EQ(mailbox.overwritten(), 0u);
}

TEST(BookMailboxTest, InvalidBooksAndErasedSymbolsAreNotDrained) {
  mmtrade::opend::BookMailbox mailbox;
  mmtrade::opend::QuoteSnapshot invalid;
  mailbox.Publish("US.BAD", invalid);
  mailbox.Publish("US.AAPL", Book(100.0, 100.1, 1));
  mailbox.Erase("US.AAPL");

  EXPECT_FALSE(mailbox.HasPending());
  EXPECT_TRUE(mailbox.Drain().empty());
}
