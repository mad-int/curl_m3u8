// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#pragma once
#include <cstdint> // uint8_t
#include <filesystem>
#include <optional>
#include <variant>
#include <vector>

using byte_t = uint8_t;

auto read_file(std::filesystem::path const& path) -> std::variant<std::vector<byte_t>, std::filesystem::filesystem_error>;
auto read_file(std::filesystem::path const& path, size_t readlen) -> std::variant<std::vector<byte_t>, std::filesystem::filesystem_error>;

auto write_file(std::filesystem::path const& path, std::vector<byte_t> const& buffer) -> std::optional<std::filesystem::filesystem_error>;

