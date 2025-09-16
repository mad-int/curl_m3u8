// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#include <format>
#include <iostream> // std::cout
#include <string>

#include "m3u8.h"

void print_usage(std::string const& progname);

int main(int argc, char** argv)
{
  if(argc < 2)
  {
    print_usage(argv[0]);
    return 1;
  }

  std::string file = argv[1];
  m3u8_t m3u8(file);

  for(auto url : m3u8.get_urls())
  {
    for(auto prop : url.properties)
      std::cout << std::format("-> {} = {}", prop.first, prop.second) << std::endl;

    int const w = 15;
    std::cout << std::format("{:.<{}}", (url.url.size() > w ? url.url.substr(0, w-3) : url.url), w) << std::endl;
  }

  return 0;
}

void print_usage(std::string const& progname)
{
  std::cout << std::format("Usage: {} <m3u8-file>", progname) << std::endl;
}

