#include <gtest/gtest.h>

#include "progressmeter.h"

TEST(progressmetertests, shorten_bytes)
{
  auto [bytes, bytes_unit] = shorten_bytes(876);
  EXPECT_EQ(bytes, 876.0);
  EXPECT_EQ(bytes_unit, "B");

  auto [kbytes, kbytes_unit] = shorten_bytes(439'376);
  EXPECT_EQ(kbytes, 429.078125);
  EXPECT_EQ(kbytes_unit, "KiB");

  auto [mbytes, mbytes_unit] = shorten_bytes(1'324'676);
  EXPECT_NEAR(mbytes, 1.2633, 0.001);
  EXPECT_EQ(mbytes_unit, "MiB");

  auto [gbytes, gbytes_unit] = shorten_bytes(24'489'324'676);
  EXPECT_NEAR(gbytes, 22.80746, 0.001);
  EXPECT_EQ(gbytes_unit, "GiB");
}

TEST(progressmetertests, calc_progressbar_filled)
{
  std::string progressbar0   = calc_progressbar_filled(  0, 100, 40);
  std::string progressbar50  = calc_progressbar_filled( 50, 100, 40);
  std::string progressbar100 = calc_progressbar_filled(100, 100, 40);

  EXPECT_EQ(progressbar0,   std::string{"                                        "});
  EXPECT_EQ(progressbar50,  std::string{"####################                    "});
  EXPECT_EQ(progressbar100, std::string{"########################################"});
}

TEST(progressmetertests, calc_progressbar_undefined)
{
  std::string progressbar0  = calc_progressbar_undefined( 0, "<->", 40);
  std::string progressbar1  = calc_progressbar_undefined( 1, "<->", 40);
  std::string progressbar2  = calc_progressbar_undefined( 2, "<->", 40);
  std::string progressbar35 = calc_progressbar_undefined(35, "<->", 40);
  std::string progressbar36 = calc_progressbar_undefined(36, "<->", 40);
  std::string progressbar37 = calc_progressbar_undefined(37, "<->", 40);
  std::string progressbar38 = calc_progressbar_undefined(38, "<->", 40);
  std::string progressbar39 = calc_progressbar_undefined(39, "<->", 40);
  std::string progressbar73 = calc_progressbar_undefined(73, "<->", 40);
  std::string progressbar74 = calc_progressbar_undefined(74, "<->", 40);
  std::string progressbar75 = calc_progressbar_undefined(75, "<->", 40);
  std::string progressbar76 = calc_progressbar_undefined(76, "<->", 40);
  std::string progressbar77 = calc_progressbar_undefined(77, "<->", 40);

  EXPECT_EQ(progressbar0,   std::string{"<->                                     "});
  EXPECT_EQ(progressbar1,   std::string{" <->                                    "});
  EXPECT_EQ(progressbar2,   std::string{"  <->                                   "});
  EXPECT_EQ(progressbar35,  std::string{"                                   <->  "});
  EXPECT_EQ(progressbar36,  std::string{"                                    <-> "});
  EXPECT_EQ(progressbar37,  std::string{"                                     <->"});
  EXPECT_EQ(progressbar38,  std::string{"                                     <->"});
  EXPECT_EQ(progressbar39,  std::string{"                                    <-> "});
  EXPECT_EQ(progressbar73,  std::string{"  <->                                   "});
  EXPECT_EQ(progressbar74,  std::string{" <->                                    "});
  EXPECT_EQ(progressbar75,  std::string{"<->                                     "});
  EXPECT_EQ(progressbar76,  std::string{"<->                                     "});
  EXPECT_EQ(progressbar77,  std::string{" <->                                    "});
}

