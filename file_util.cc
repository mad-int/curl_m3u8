// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#include <cassert>
#include <fstream>
#include <system_error> // std::error_code

#include "file_util.h"

auto read_file(std::filesystem::path const& path) -> std::variant<std::vector<byte_t>, std::filesystem::filesystem_error>
{
  std::ifstream file{path};
  if(file.fail())
  {
    int const err = errno;
    auto errc = std::error_code{err, std::generic_category()};
    return std::filesystem::filesystem_error{"Couldn't open file for reading", path, errc};
  }

  file.seekg(0, std::ios::end);
  std::streamsize const size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<byte_t> buffer(size);
  file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
  if(file.fail())
  {
    int const err = errno;
    auto errc = std::error_code{err, std::generic_category()};
    return std::filesystem::filesystem_error{"Couldn't read file", path, errc};
  }

  return buffer;
}

auto read_file(std::filesystem::path const& path, size_t maxbytes) -> std::variant<std::vector<byte_t>, std::filesystem::filesystem_error>
{
  std::ifstream file{path};
  if(file.fail())
  {
    int const err = errno;
    auto errc = std::error_code{err, std::generic_category()};
    return std::filesystem::filesystem_error{"Couldn't open file for reading", path, errc};
  }

  file.seekg(0, std::ios::end);
  std::streamsize const size = file.tellg();
  file.seekg(0, std::ios::beg);

  assert(size >= 0);

  std::vector<byte_t> buffer(size < static_cast<std::streamsize>(maxbytes) ? size : maxbytes);
  file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
  if(file.fail())
  {
    int const err = errno;
    auto errc = std::error_code{err, std::generic_category()};
    return std::filesystem::filesystem_error{"Couldn't read file", path, errc};
  }

  return buffer;
}

auto write_file(std::filesystem::path const& path, std::vector<byte_t> const& buffer) -> std::optional<std::filesystem::filesystem_error>
{
  std::ofstream file{path};
  if(file.fail())
  {
    int const err = errno;
    auto errc = std::error_code{err, std::generic_category()};
    return std::filesystem::filesystem_error{"Couldn't open file for writing", path, errc};
  }

  file.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  if(file.fail())
  {
    int const err = errno;
    auto errc = std::error_code{err, std::generic_category()};
    return std::filesystem::filesystem_error{"Couldn't write file", path, errc};
  }

  return {};
}

