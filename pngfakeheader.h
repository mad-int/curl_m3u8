// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#pragma once
#include <cstdint> // uint8_t
#include <cstring> // memcmp()
#include <filesystem>
#include <variant>
#include <vector>

#include "file_util.h"

// The PNG fake-header is a 1x1 pixel png.
static std::vector<uint8_t> const png_fake_header{
  0x89, 0x50, 0x4e, 0x47,  0x0d, 0x0a, 0x1a, 0x0a, // magic bytes of PNG: "%PNG"<CR><LF><SUB><LF>
  0x00, 0x00, 0x00, 0x0d,  0x49, 0x48, 0x44, 0x52,
  0x00, 0x00, 0x00, 0x01,  0x00, 0x00, 0x00, 0x01,
  0x08, 0x06, 0x00, 0x00,  0x00, 0x1f, 0x15, 0xc4,
  0x89, 0x00, 0x00, 0x00,  0x0d, 0x49, 0x44, 0x41,
  0x54, 0x78, 0x9c, 0x63,  0x60, 0x60, 0x60, 0x60,
  0x00, 0x00, 0x00, 0x05,  0x00, 0x01, 0xa5, 0xf6,
  0x45, 0x40, 0x00, 0x00,  0x00, 0x00, 0x49, 0x45,
  0x4e, 0x44, 0xae, 0x42,  0x60, 0x82};

//! Checks for the PNG fake-header and if it exists, removes it from the file.
static auto check_and_remove_pngfakeheader(std::filesystem::path const& path) -> std::variant<bool, std::filesystem::filesystem_error>
{
  namespace fs = std::filesystem;

  // Check for the PNG fake-header.
  {
    // Read a byte more than the size of the PNG fake-header from file,
    // to ensure that there comes stuff behind the header.
    auto buffer_error = read_file(path, png_fake_header.size() + 1);
    if(std::holds_alternative<fs::filesystem_error>(buffer_error))
      return std::get<fs::filesystem_error>(buffer_error);
    // else
    auto file_buffer = std::get<std::vector<byte_t>>(buffer_error);

    bool const has_png_fake_header = file_buffer.size() > png_fake_header.size()
      and std::memcmp(file_buffer.data(), png_fake_header.data(), png_fake_header.size()) == 0;

    if(not has_png_fake_header)
      return false;
    // else
  }

  // Remove the PNG fake-header.
  {
    auto buffer_error = read_file(path);
    if(std::holds_alternative<fs::filesystem_error>(buffer_error))
      return std::get<fs::filesystem_error>(buffer_error);
    // else
    auto file_buffer = std::get<std::vector<byte_t>>(buffer_error);

    file_buffer.erase(file_buffer.begin(), file_buffer.begin() + png_fake_header.size());
    auto maybe_error = write_file(path, file_buffer);
    if(maybe_error.has_value())
      return maybe_error.value();
  }

  return true;
}

