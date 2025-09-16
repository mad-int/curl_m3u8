#pragma once

#include <algorithm> // std::find_if
#include <string>

#include <cassert>

/**
 * Trim a string left and right.
 */
static inline std::string trim(std::string s)
{
  // left trim
  std::string::iterator space_end =
      std::find_if(s.begin(), s.end(), [](int ch) { return not std::isspace(ch); });
  s.erase(s.begin(), space_end);

  // right trim
  std::string::iterator space_begin =
    std::find_if(s.rbegin(), s.rend(), [](int ch) { return not std::isspace(ch); }).base();
  s.erase(space_begin, s.end());

  return s;
}

/**
 * Tokenize a string according to a given delimiter (default: ',').
 *
 * Empty tokens are discared. The tokens are not trimmed.
 */
static inline auto tokenize(std::string s, char delim = ',') -> std::vector<std::string>
{
  std::vector<std::string> ret = {};

  size_t prev = 0, pos = s.find(delim, 0);
  while(pos != std::string::npos)
  {
    std::string token = s.substr(prev, pos - prev);
    if(not token.empty())
      ret.push_back(token);

    prev = pos+1;
    pos = s.find(delim, pos+1);
  }

  { // pos == std::string::npos
    std::string token = s.substr(prev);
    if(not token.empty())
      ret.push_back(token);
  }

  return ret;
}

