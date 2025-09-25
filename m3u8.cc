// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#include <cassert>
#include <cstring> // strerror
#include <filesystem>
#include <fstream>
#include <ranges>
#include <regex>
#include <system_error> // std::error_code
#include <variant>

#include <format>
#include <iostream>

#include "m3u8.h"
#include "string_util.h"

namespace fs = std::filesystem;

static constexpr std::string EXTM3U = "#EXTM3U";

auto parse_m3u8(std::istream& istream) -> std::variant<std::vector<urlprops_t>, m3u8_errc>;
auto parse_extinf(std::string const& line) -> std::map<std::string, std::string>;
auto parse_extxstreaminfo(std::string const& line) -> std::map<std::string, std::string>;

auto tokenize_properties(std::string const& info) -> std::vector<std::string>;
auto parse_properties(std::vector<std::string> const& props) -> std::map<std::string, std::string>;
auto parse_property(std::string const& prop) -> std::tuple<std::string, std::string>;

// ---

void m3u8_t::parse_m3u8(std::istream& istream)
{
  assert(not istream.fail());

  std::string line = "";
  std::getline(istream, line);

  if(line != EXTM3U)
  {
    m_error = m3u8_errc::wrong_file_format;
    return;
  }

  std::vector<urlprops_t> urls = {};

  std::map<std::string, std::string> properties = {};

  while(std::getline(istream, line))
  {
    if(line.starts_with("#EXT-X-STREAM-INF:"))
    {
      auto props = parse_extxstreaminfo(line);
      for(auto const& prop : props)
        properties[prop.first] = prop.second;

      m_master = true;
    }
    else if(line.starts_with("#EXTINF:"))
    {
      auto props = parse_extinf(line);
      for(auto const& prop : props)
        properties[prop.first] = prop.second;

      m_playlist = true;
    }
    else if(not line.starts_with("#") and not line.empty())
    {
      urls.push_back(urlprops_t{line, properties});
      properties = {};
    }
    else if(line.empty())
    {
      properties = {};
    }
    // else // line starts with # -> unsupported, ignore it
  }

  m_urls = urls;
}

//! Format is "#EXTINF:RUNTIME (KEY1=VALUE1, KEY2=VALUE2, ...)?(, DISPLAY-TITLE)?"
//! see https://en.wikipedia.org/wiki/M3U
auto parse_extinf(std::string const& line) -> std::map<std::string, std::string>
{
  assert(line.starts_with("#EXTINF:"));

  auto pos = line.find(':');
  if(pos == std::string::npos)
    return {};

  std::string info = line.substr(pos+1);

  auto tokens = tokenize_properties(info);
  if(tokens.size() == 0)
    return {};

  auto first_token = trim(tokens.front());
  tokens.erase(tokens.begin());
  auto last_token = not tokens.empty() ? trim(tokens.back()) : "";
  if(not tokens.empty())
    tokens.erase(tokens.end()-1);

  auto properties = parse_properties(tokens);

  // First token should be the runtime.
  if(not first_token.contains('=')) // Good
  {
    // Should I verify that the runtime-token is a number (-1, 0, \d)?
    properties["RUNTIME"] = first_token;
  }
  else // Bad: Is not the runtime but an property.
  {
    auto const [key, value] = parse_property(first_token);
    properties[key] = value;
  }

  // The optional last token should be the display title.
  if(not last_token.empty())
  {
    if(not last_token.contains('=')) // Good
    {
      properties["DISPLAY-TITLE"] = last_token;
    }
    else // Bad: Is not the runtime but an property.
    {
      auto const [key, value] = parse_property(last_token);
      properties[key] = value;
    }
  }

  return properties;
}

//! Format is "#EXT-X-STREAM-INF: (KEY1=VALUE1, KEY2=VALUE2, ...)"
//! see https://en.wikipedia.org/wiki/M3U
auto parse_extxstreaminfo(std::string const& line) -> std::map<std::string, std::string>
{
  assert(line.starts_with("#EXT-X-STREAM-INF:"));

  auto pos = line.find(':');
  if(pos == std::string::npos)
    return {};

  std::string info = line.substr(pos+1);

  auto tokens = tokenize_properties(info);
  if(tokens.size() == 0)
    return {};

  return parse_properties(tokens);
}

//! The info-string is of format: (KEY1=VALUE1, KEY2=VALUE2, ...)
//! Caution: The value-strings can be quotation-mark string ("...") that contain commas
//  e.g. CODECS="mp4a.40.2,avc1.42c01e".
//                        ^--- !!!
auto tokenize_properties(std::string const& info) -> std::vector<std::string>
{
  assert((not info.starts_with('#')) and "call with only the info-part and not the complete line");

  // TODO: Instead of dummy tokenize the info-string at every comma
  // and fix the commas inside of quotation-mark strings ("...") afterwards
  // use a tokenizer that can handle quotation-mark strings.
  // But for now is garbage-in-garbage-out.
  auto tokens = tokenize(info, ',');
  if(tokens.size() == 0)
    return {};

  // Fix "... , ..."-strings.
  decltype(tokens) fixed_tokens = {};
  std::string quotstr = "";
  for(auto token : tokens)
  {
    if(not quotstr.empty())
    {
      // Either token contains no quotation-mark (")
      // or exactly one at the end.
      assert( (std::ranges::count(token, '\"') == 0)
          or ((std::ranges::count(token, '\"') == 1) and token.ends_with('\"')));
      quotstr += "," + token;

      if(token.ends_with('\"'))
      {
        fixed_tokens.push_back(quotstr);
        quotstr = "";
      }
    }
    // e.g. token is "CODECS=\"mp4a.40.2"
    else if(quotstr.empty() and std::ranges::count(token, '\"') == 1)
    {
      quotstr = token;
    }
    else
    {
      // Either token contains no quotation-mark (")
      // or exactly two with one at the end.
      assert( (std::ranges::count(token, '\"') == 0)
          or ((std::ranges::count(token, '\"') == 2) and token.ends_with('\"')));

      fixed_tokens.push_back(token);
    }
  }

  return fixed_tokens;
}

//! Parse "KEY=VALUE" strings.
auto parse_properties(std::vector<std::string> const& props) -> std::map<std::string, std::string>
{
  std::map<std::string, std::string> ret = {};

  for(auto const& prop : props)
  {
    if(prop.contains('='))
    {
      auto const [key, value] = parse_property(prop);
      if(not ret.contains(key))
        ret[key] = value;
      // otherwise discard -> first occurrence wins
    }
    // else Isn't a property, but something else!?
  }

  return ret;
}

//! Parse a "KEY=VALUE" string.
auto parse_property(std::string const& prop) -> std::tuple<std::string, std::string>
{
  assert(prop.contains('='));

  auto sign_pos = prop.find('=');
  if(sign_pos == std::string::npos) // Bad: Isn't a property, but only a value!?
    return std::make_tuple("", prop);

  std::string const key   = trim(prop.substr(0, sign_pos));
  std::string value = trim(prop.substr(sign_pos+1));
  if(value.length() >= 2 and value[0] == '\"' and value[value.length() - 1] == '\"')
  {
    value.erase(value.begin());
    value.erase(value.length() - 1);
  }

  return std::make_tuple(key, value);
}

// ---

m3u8_t::m3u8_t(std::filesystem::path const& path)
  : m_urls{}, m_master(false), m_playlist(false), m_error{}
{
  assert(std::ranges::count(path.string(), '\n') == 0 and "Given argument is not a path!");

  std::ifstream file{path};
  if(file.fail())
  {
    int const err = errno;
    m_error = fs::filesystem_error{ "Couldn't open file", path, std::error_code{err, std::generic_category()} };
    return;
  }

  parse_m3u8(file);
}

m3u8_t::m3u8_t(std::vector<char> const& buffer)
  : m_urls{}, m_master(false), m_playlist(false), m_error{}
{
  std::stringstream ss{std::string(buffer.data(), buffer.size())};

  parse_m3u8(ss);
}

/** For testing.
 * urls.size() == 5 set both is_master() and is_playlist() true.
 * It can happen that both bools are set to true.
 */
m3u8_t::m3u8_t(std::vector<urlprops_t> urls)
  : m_urls{urls}, m_master(urls.size() <= 5), m_playlist(urls.size() >= 5), m_error{}
{
}

// ---

bool is_absolute_url(urlprops_t const& urlprops);

bool m3u8_t::contains_absolute_urls() const
{
  for(auto url : m_urls)
  {
    if(is_absolute_url(url))
      return true;
  }

  return false;
}

bool m3u8_t::contains_relative_urls() const
{
  for(auto url : m_urls)
  {
    if(not is_absolute_url(url))
      return true;
  }

  return false;
}

bool is_absolute_url(urlprops_t const& urlprops)
{
  return std::regex_match(urlprops.url, std::regex("^\\w{3,5}://.*$"));
}

auto get_urlbase(std::string const& url) -> std::string
{
  std::smatch results;
  if(std::regex_match(url, results, std::regex("^(\\w{3,5}://[^/]*)/.*$")))
    return results[1];
  return "";
}

auto get_urlpath(std::string const& url) -> std::string
{
  auto const pos = url.find_last_of('/');
  return pos != std::string::npos ? url.substr(0, pos) : "";
}

void m3u8_t::set_urlprefix(std::string const& prefix)
{
  assert(not prefix.empty());

  for(auto& url : m_urls)
  {
    assert(not url.url.empty());

    if(not is_absolute_url(url))
    {
      auto prefix_length = prefix.length();
      while(prefix.substr(0, prefix_length).ends_with('/'))
        prefix_length--;

      while(url.url.starts_with('/'))
        url.url.erase(url.url.begin());

      url.url = prefix.substr(0, prefix_length) +  "/" + url.url;
    }
  }
}

// ---

auto is_m3u8(fs::path const& path) -> std::variant<bool, fs::filesystem_error>
{
  std::ifstream file{path};
  if(file.fail())
  {
    int const err = errno;
    return fs::filesystem_error{ "Couldn't open file", path, std::error_code{err, std::generic_category()} };
  }

  std::string line = "";
  std::getline(file, line);

  return line == EXTM3U;
}

bool is_m3u8(std::vector<char> const& buffer)
{
  // with C++26
  //std::string_view view(buffer.data());
  //ss.str(view);

  if(buffer.size() < EXTM3U.size())
    return false;

  std::stringstream ss{std::string(buffer.data(), EXTM3U.size())};

  std::string line = "";
  std::getline(ss, line);

  return std::string(buffer.data(), EXTM3U.size()) == EXTM3U;
}

