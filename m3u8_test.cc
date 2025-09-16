#include <gtest/gtest.h>
#include <filesystem>
#include <variant>

namespace fs = std::filesystem;

#include "m3u8.h"

bool is_absolute_url(urlprops_t const& urlprops); // defined in m3u8.cc

std::string const master_m3u8_str =
  "#EXTM3U\n"
  "#EXT-X-INDEPENDENT-SEGMENTS\n"
  "#EXT-X-STREAM-INF:BANDWIDTH=716090,CODECS=\"mp4a.40.2,avc1.42c01e\",RESOLUTION=640x360,FRAME-RATE=24,VIDEO-RANGE=SDR,CLOSED-CAPTIONS=NONE\n"
  "/path1/index.m3u8\n"
  "#EXT-X-STREAM-INF:BANDWIDTH=2999153,CODECS=\"mp4a.40.2,avc1.64001f\",RESOLUTION=1280x720,FRAME-RATE=24,VIDEO-RANGE=SDR,CLOSED-CAPTIONS=NONE\n"
  "/path2/index.m3u8\n"
  "#EXT-X-STREAM-INF:BANDWIDTH=5627358,CODECS=\"mp4a.40.2,avc1.640028\",RESOLUTION=1920x1080,FRAME-RATE=24,VIDEO-RANGE=SDR,CLOSED-CAPTIONS=NONE\n"
  "/path3/index.m3u8\n"
;
std::vector<char> const master_m3u8{master_m3u8_str.begin(), master_m3u8_str.end()};

TEST(m3u8_tests, get_urls)
{
  m3u8_t master(master_m3u8);

  auto urls = master.get_urls();

  ASSERT_EQ(urls.size(), 3);

  EXPECT_EQ(urls[0].url, "/path1/index.m3u8");
  EXPECT_EQ(urls[0].properties.size(), 6);
  EXPECT_EQ(urls[0].properties["BANDWIDTH"], "716090");
  EXPECT_EQ(urls[0].properties["CODECS"], "mp4a.40.2,avc1.42c01e");
  EXPECT_EQ(urls[0].properties["RESOLUTION"], "640x360");
  EXPECT_EQ(urls[0].properties["FRAME-RATE"], "24");
  EXPECT_EQ(urls[0].properties["VIDEO-RANGE"], "SDR");
  EXPECT_EQ(urls[0].properties["CLOSED-CAPTIONS"], "NONE");

  EXPECT_EQ(urls[1].url, "/path2/index.m3u8");
  EXPECT_EQ(urls[0].properties.size(), 6);
  EXPECT_EQ(urls[1].properties["CODECS"], "mp4a.40.2,avc1.64001f");
  EXPECT_EQ(urls[1].properties["RESOLUTION"], "1280x720");

  EXPECT_EQ(urls[2].url, "/path3/index.m3u8");
  EXPECT_EQ(urls[0].properties.size(), 6);
  EXPECT_EQ(urls[2].properties["CODECS"], "mp4a.40.2,avc1.640028");
  EXPECT_EQ(urls[2].properties["RESOLUTION"], "1920x1080");
}

TEST(m3u8_tests, set_baseurl)
{
  std::vector<urlprops_t> const urls = {
    urlprops_t{"https://server/path1", {}},
    urlprops_t{"/path2", {}},
    urlprops_t{"/path3/", {}},
  };

  m3u8_t master{urls};

  master.set_baseurl("https://server/");

  ASSERT_EQ(master.get_urls().size(), 3);
  EXPECT_EQ(master.get_url(0).url, std::string{"https://server/path1"});
  EXPECT_EQ(master.get_url(1).url, std::string{"https://server/path2"});
  EXPECT_EQ(master.get_url(2).url, std::string{"https://server/path3/"});
}

TEST(m3u8_tests, get_baseurl)
{
  EXPECT_EQ(get_baseurl("https://server/path"), std::string{"https://server"});
  EXPECT_EQ(get_baseurl("http://server/dir1/dir2/dir3/"), std::string{"http://server"});
  EXPECT_EQ(get_baseurl("ftp://server/./dir2/dir3/"), std::string{"ftp://server"});
}


TEST(m3u8_tests, is_m3u8_ok)
{
  EXPECT_TRUE(is_m3u8(master_m3u8));

  // ---

  auto result = is_m3u8("weapons/master.m3u8");

  ASSERT_TRUE(std::holds_alternative<bool>(result));
  EXPECT_TRUE(std::get<bool>(result));
}

TEST(m3u8_tests, is_m3u8_fail)
{
  auto result = is_m3u8("doesnt_exist.m3u8");

  EXPECT_TRUE(std::holds_alternative<fs::filesystem_error>(result));

  //auto fs_error = std::get<fs::filesystem_error>(result);
  //std::cerr << fs_error.code().value() << " - " << fs_error.code().message() << std::endl;
  //std::cerr << fs_error.what() << std::endl;
}

TEST(m3u8_tests, is_absolute_url)
{
  EXPECT_TRUE(is_absolute_url(urlprops_t{"ftp://server/path", {}}));
  EXPECT_TRUE(is_absolute_url(urlprops_t{"http://server/path", {}}));
  EXPECT_TRUE(is_absolute_url(urlprops_t{"https://server/path", {}}));

  EXPECT_FALSE(is_absolute_url(urlprops_t{"/path", {}}));
  EXPECT_FALSE(is_absolute_url(urlprops_t{"path", {}}));
}

