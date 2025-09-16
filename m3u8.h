// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#pragma once
#include <filesystem>
#include <istream>
#include <optional>
#include <map>
#include <string>
#include <variant>
#include <vector>

//
// "Supportes" only a tiny subset (#EXTINF and #EXT-X-STREAM-INF somewhat) of M3U.
// The spec is https://datatracker.ietf.org/doc/html/rfc8216.
//

auto is_m3u8(std::filesystem::path const& path) -> std::variant<bool, std::filesystem::filesystem_error>;
auto is_m3u8(std::vector<char> const& buffer) -> bool;

//! Get the baseurl from a complete url.
//! The baseurl is the protocol + the host.
auto get_baseurl(std::string const& url) -> std::string;

//! See std::io_errc or std::future_errc how this can be extended.
enum class m3u8_errc
{
  wrong_file_format = 1,
};

struct urlprops_t
{
  std::string url;
  std::map<std::string, std::string> properties;
};

class m3u8_t
{
public:

  explicit m3u8_t(std::filesystem::path const& filepath);
  explicit m3u8_t(std::vector<char> const& buffer);

  m3u8_t(m3u8_t const& other) = default;
  m3u8_t(m3u8_t&& other) = default;
  ~m3u8_t() = default;

  m3u8_t& operator=(m3u8_t const& other) = default;
  m3u8_t& operator=(m3u8_t&& other) = default;

  inline bool is_master() const { return m_master; }
  inline bool is_playlist() const { return m_playlist; }

  inline auto get_urls() const -> std::vector<urlprops_t> { return m_urls; }
  inline auto get_url(size_t i) const -> urlprops_t { return m_urls[i]; }

  bool contains_absolute_urls() const;
  bool contains_relative_urls() const;

  void set_baseurl(std::string const& baseurl);

  // For testing.
  explicit m3u8_t(std::vector<urlprops_t> urls);


public:

  bool occured_error() const { return m_error.has_value(); }

  auto get_error() const -> std::optional<std::variant<m3u8_errc, std::filesystem::filesystem_error>>
  {
    return m_error;
  }


protected:

  void parse_m3u8(std::istream& istream);


private:

  std::vector<urlprops_t> m_urls = {};
  bool m_master = false;
  bool m_playlist = false;

  std::optional<std::variant<m3u8_errc, std::filesystem::filesystem_error>> m_error = {};
};

