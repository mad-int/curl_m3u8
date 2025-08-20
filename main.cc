#include <format>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <variant>

#include <cstring> // strerror()

#include <getopt.h>

#include "curl_wrapper.h"
#include "string_util.h"

// ---

struct cmdline_t
{
  int help_flag = 0;
  int verbose_flag = 0;

  std::string url = "";
};

auto read_urls_from_m3u8(const std::string filename) -> std::vector<std::string>;

auto parse_options(int argc, char* argv[]) -> std::optional<cmdline_t>;
void print_usage(const char* progname);

// ---

int main(int argc, char** argv)
{
  auto cmdline_result = parse_options(argc, argv);
  if(not cmdline_result.has_value())
  {
    print_usage(argv[0]);
    std::exit(-1);
  }
  cmdline_t const cmdline = cmdline_result.value();

  if(cmdline.help_flag)
  {
    print_usage(argv[0]);
    std::exit(1);
  }

  int ret = 0;
  {
    curl_wrapper::init();

    curl_wrapper curl_m3u8;
    if(cmdline.verbose_flag)
      curl_m3u8.set_verbose();
    //curl_m3u8.set_default_progress_meter();

    std::string const url = cmdline.url;
    std::string const file = "index.m3u8"; // curl_m3u8.get_filename_from_url(url);
    if(file == "")
    {
      std::cerr << std::format("Error: No valid file found in url `{}'!", url) << std::endl;
      ret = -2;
    }
    else if((file.size() <= 5) || (file.substr(static_cast<size_t>(file.size()-5)) != ".m3u8"))
    {
      std::cerr << std::format("Warning: Suffix is not m3u8 of file `{}'!", file.c_str()) << std::endl;
    }

    if(ret == 0)
    {
      auto result = curl_m3u8.download_file("index.m3u8", url);
      if(std::holds_alternative<curl_wrapper_error>(result))
      {
        auto error = std::get<curl_wrapper_error>(result);
        std::cerr << std::format("Error: {}!", error.what()) << std::endl;
        ret = -3;
      }
      else
      {
        //auto buffer = std::get<std::vector<char>>(result);
        //std::ranges::copy(buffer, std::ostream_iterator<char>(std::cout, ""));
      }
    }

    /*std::string const filename = curl_m3u8.download_file(file, url);
      std::vector<std::string> urls = read_urls_from_m3u8(filename);

      for(size_t i=0; i<(5 < urls.size() ? 5 : urls.size()); i++)
      {
      std::cout << urls[i] << std::endl;
      }*/

    //
    // start async operations of the chunks
    //

    //curl_m3u8.default_progress_meter(true);

    /*std::vector<std::string> urls {
      "https://arteptweb-vh.akamaihd.net/i/am/ptweb/083000/083900/083960-000-A_0_VA-STA_AMM-PTWEB_XQ.1FPwq1DStxq.smil/segment1_1_av.ts",
      "https://arteptweb-vh.akamaihd.net/i/am/ptweb/083000/083900/083960-000-A_0_VA-STA_AMM-PTWEB_XQ.1FPwq1DStxq.smil/segment2_1_av.ts",
      "https://arteptweb-vh.akamaihd.net/i/am/ptweb/083000/083900/083960-000-A_0_VA-STA_AMM-PTWEB_XQ.1FPwq1DStxq.smil/segment3_1_av.ts",
      "https://arteptweb-vh.akamaihd.net/i/am/ptweb/083000/083900/083960-000-A_0_VA-STA_AMM-PTWEB_XQ.1FPwq1DStxq.smil/segment4_1_av.ts"
      };*/

    //curl_m3u8.download_files(urls);

    curl_wrapper::cleanup();
  }

  return ret;
}

// ---

std::vector<std::string> read_urls_from_m3u8(const std::string filename)
{
  std::vector<std::string> ret;

  FILE* m3u8 = fopen(filename.c_str(), "r");
  if(m3u8 == nullptr)
  {
    const int err = errno;
    fprintf(stderr, "Error: Can't open file `%s' for reading: %s\n", filename.c_str(), strerror(err));
    return std::vector<std::string> {};
  }
  // else

  size_t buffer_length = 256;
  char* buffer = static_cast<char*>( malloc(sizeof(char)*buffer_length) );

  ssize_t read_bytes = getline(&buffer, &buffer_length, m3u8);
  if(read_bytes == -1 and not feof(m3u8))
  {
    const int err = errno;
    fprintf(stderr, "Error: Reading from file `%s' failed: %s\n", filename.c_str(), strerror(err));
    free(buffer);
    fclose(m3u8);
    return std::vector<std::string> {};
  }
  else if(feof(m3u8))
  {
    fprintf(stderr, "Error: File `%s' is empty!\n", filename.c_str());
    free(buffer);
    fclose(m3u8);
    return std::vector<std::string> {};
  }
  else if(trim(std::string {buffer}) != "#EXTM3U")
  {
    fprintf(stderr, "Error: File format of `%s' is not m3u8!\n", filename.c_str());
    free(buffer);
    fclose(m3u8);
    return std::vector<std::string> {};
  }
  // else

  while((read_bytes = getline(&buffer, &buffer_length, m3u8)) != -1)
  {
    const std::string line = trim(std::string {buffer});

    if(line == "#EXT-X-ENDLIST")
      break;

    if(line == "" or line[0] == '#')
      continue;

    ret.push_back(line);
  }

  if(read_bytes == -1 and not feof(m3u8))
  {
    const int err = errno;
    fprintf(stderr, "Error: Reading from file `%s' failed: %s\n", filename.c_str(), strerror(err));
    free(buffer);
    fclose(m3u8);
    return std::vector<std::string> {};
  }

  free(buffer);
  fclose(m3u8);

  return ret;
}

// ---

void print_usage(const char* progname)
{
  printf("Usage: %s [-v|--verbose] <URL>\n", progname);
}

auto parse_options(int argc, char* argv[]) -> std::optional<cmdline_t>
{
  cmdline_t cmdline;

  // Usage: <argv[0]> [--verbose|-v] <URL>
  constexpr char const* short_options = "hv";
  struct option long_options[] =
  {
    // long name, no_argument|required_argument, flag, val or nullptr
    {"help", no_argument, &cmdline.help_flag, 1},
    {"verbose", no_argument, &cmdline.verbose_flag, 1},
    {nullptr, 0, nullptr, 0}
  };

  int parsed_options = 1; // argv[0] is the program name.

  int c = 0;
  int option_index = 0;
  while((c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1)
  {
    switch(c)
    {
      // long option
      // Use option_index to distinguish them.
      case 0:
        parsed_options++;
        break;

      // short options
      case 'v':
        cmdline.verbose_flag = 1;
        parsed_options++;
        break;

      case 'h':
        cmdline.help_flag = 1;
        parsed_options++;
        break;

      case '?': // getopt_long printed an error-message.
      default:
        return {};
    }
  }

  if(cmdline.help_flag)
    return cmdline;

  cmdline.url = parsed_options < argc ? argv[parsed_options] : "";

  if(parsed_options == argc) // Everything was parsed, so URL was forgotten.
  {
    std::cerr << "Error: URL needs to be provided!" << std::endl;
    return {};
  }
  else if(parsed_options+1 < argc) // Trailing stuff.
  {
    std::cerr << std::format("Error: Trailing stuff `{}' found!", argv[parsed_options+1]) << std::endl;
    return {};
  }
  // else parsed_options+1 == argc - Good case continue, everything was parsed + our url.
  return cmdline;
}

