// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#include <gtest/gtest.h>

#include "string_util.h"

TEST(stirng_util_tests, trim)
{
  EXPECT_EQ(trim("   Value 1   "),      "Value 1");
}

TEST(string_util_tests, tokenize)
{
  EXPECT_EQ(tokenize("").size(), 0);

  // ---

  auto const tokens1 = tokenize("one token");

  ASSERT_EQ(tokens1.size(), 1);
  EXPECT_EQ(tokens1[0], "one token");

  // ---

  auto const tokens2 = tokenize(";;;token1;token2;", ';');

  ASSERT_EQ(tokens2.size(), 2);
  EXPECT_EQ(tokens2[0], "token1");
  EXPECT_EQ(tokens2[1], "token2");
}

TEST(string_util_tests, calc_numberlength)
{
  EXPECT_EQ(calc_numberlength(      0), 1);
  EXPECT_EQ(calc_numberlength(      5), 1);
  EXPECT_EQ(calc_numberlength(      9), 1);
  EXPECT_EQ(calc_numberlength(     10), 2);
  EXPECT_EQ(calc_numberlength(     50), 2);
  EXPECT_EQ(calc_numberlength(     99), 2);
  EXPECT_EQ(calc_numberlength(    100), 3);

  EXPECT_EQ(calc_numberlength(    500), 3);
  EXPECT_EQ(calc_numberlength(   1500), 4);
  EXPECT_EQ(calc_numberlength(  10500), 5);
  EXPECT_EQ(calc_numberlength( 100500), 6);
  EXPECT_EQ(calc_numberlength(1000500), 7);
}

