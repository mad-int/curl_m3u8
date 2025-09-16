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

