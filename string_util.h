#pragma once

#include <algorithm> // std::find_if
#include <sstream>
#include <string>

#include <cassert>

//

/**
 * Left and right trim sting \p s.
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

//

/**
 * Formated output conversion with std::string.
 *
 * Implements a subset of sprintf() with std::string.
 * The result is returned as std::string.
 */
template<typename... Args>
std::string strprintf(const std::string format, Args... args);

/**
 * Formated output conversion with std::stringstream.
 *
 * Implements a subset of sprintf() with std::stringstream \p ss.
 * The result is the std::stringsteam \p ss returned as std::string (via ss.str()).
 */
template<typename T, typename... Args>
std::string ssprintf(std::stringstream& ss, const std::string format, const T first, Args... args);

/**
 * Formated output conversion with std::stringstream.
 *
 * Implements a subset of sprintf() with std::stringstream \p ss.
 * The result is the std::stringsteam \p ss returned as std::string (via ss.str()).
 */
static inline std::string ssprintf(std::stringstream& ss, std::string format);

//

template<typename... Args>
std::string strprintf(const std::string format, Args... args)
{
  std::stringstream ss;
  return ssprintf(ss, format, args...);
}

template<typename T, typename... Args>
std::string ssprintf(std::stringstream& ss,
    std::string format, const T first, Args... args)
{
  size_t pos = 0;
  while((pos = format.find("%")) != std::string::npos)
  {
    assert(pos+1 < format.length());

    if(format[pos+1] != '%')
      break;
    // else -> found "%%"

    const std::string start = format.substr(0, pos);
    ss << start << '%';

    format = format.substr(pos+2);
  }

  assert(pos != std::string::npos);
  assert(pos+1 < format.length());

  const std::string start = format.substr(0, pos);
  const std::string end = format.substr(pos+2);

  switch(format[pos+1])
  {
    case '%': // "%%" -> '%'
      ss << start << '%';
      break;

    case 'c': // "%c"
      assert(std::is_integral<T>::value);
      char c;
      {
        std::stringstream str;
        str << first;
        str >> c;
      }
      ss << start << c;
      break;

    case 'd': // "%d"
      assert(std::is_integral<T>::value);
      ss << start << std::dec << first;
      break;

    case 'x': // "%x"
      assert(std::is_integral<T>::value);
      ss << start << std::hex << std::showbase << first;
      break;

    case 'X': // "%X"
      assert(std::is_integral<T>::value);
      ss << start << std::hex << std::showbase << std::uppercase << first;
      break;

    case 'o': // "%o"
      assert(std::is_integral<T>::value);
      ss << start << std::oct << std::showbase << first;
      break;

    case 's': // "%s"
      ss << start << first;
      break;

    default: // unknown conversion specifier
      assert(false);
  }

  return ssprintf(ss, format.substr(pos+2), args...);
}

static std::string ssprintf(std::stringstream& ss, std::string format)
{
  size_t pos = 0;
  while((pos = format.find("%")) != std::string::npos)
  {
    assert(pos+1 < format.length());
    assert(format[pos+1] == '%'); // No conversion specifiers are allowed, only "%%".

    const std::string start = format.substr(0, pos);
    ss << start << '%';

    format = format.substr(pos+2);
  }

  ss << format;
  return ss.str();
}

