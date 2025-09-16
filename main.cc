// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#include <cmath>    // std::pow()
#include <cstdio>   // std::remove()
#include <cstring>  // std::strerror()
#include <cstdlib>  // std::system()
#include <format>
#include <fstream>  // std::ofstream
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <variant>

#include <getopt.h>
#include <sys/wait.h> // WEXITSTATUS
#include <termios.h>  // see function getch() below

#include "curl_wrapper.h"
#include "m3u8.h"
#include "string_util.h"

// ---

struct cmdline_t
{
  bool help_flag = false;
  bool verbose_flag = false;

  std::string name = "";
  std::string url = "";
};

bool check_command(std::string const& cmd);
auto download_m3u8(curl_wrapper const& curl, std::string const& url) -> m3u8_t; // throws on error
auto pick_playlist(m3u8_t const& m3u8) -> int;
int concat_ffmpeg(std::string const& name, std::vector<std::filesystem::path> const& parts);

auto read_urls_from_m3u8(const std::string filename) -> std::vector<std::string>;

auto parse_options(int argc, char* argv[]) -> std::optional<cmdline_t>;
void print_usage(const char* progname);

//! Read a single key-press from the keyboard (without enter).
//! The ASCII-code is returned (e.g. 'c' for the c-key).
static char getch(bool echo = true);

// ---

// TODO: on verbose implement more logging
// TODO: Instead of the local directory, use a subdir in temp.
// TODO: progressmeter add a total line to output
// TODO: cmake install target (maybe also Debug and Release target)
// TODO: support cancel
// TODO: support continue after halfway canceled download

int main(int argc, char** argv)
{
  auto const cmdline_result = parse_options(argc, argv);
  bool const exists_ffmpeg = check_command("ffmpeg --help");

  int ret = 0;
  if(not exists_ffmpeg)
  {
    std::cerr << "Error: ffmpeg was not found!" << std::endl;
    ret = -2;
  }

  if(not cmdline_result.has_value())
  {
    print_usage(argv[0]);
    return ret ? ret : -1;
  }
  cmdline_t const cmdline = cmdline_result.value();

  if(cmdline.help_flag)
  {
    print_usage(argv[0]);
    return ret ? ret : 0;
  }

  if(ret != 0)
    return ret;

  curl_wrapper::init();

  try
  {
    curl_wrapper curl;

    if(cmdline.verbose_flag)
      curl.set_verbose();
    //curl.set_default_progressmeter();

    std::string const url = cmdline.url;
    std::string const name = cmdline.name;

    bool cancel = false;

    //
    // 1. Download m3u8-file(s)
    //
    m3u8_t m3u8 = download_m3u8(curl, url);
    if(m3u8.is_master()) // Pick and download playlist m3u8-file.
    {
      int i = pick_playlist(m3u8);
      cancel = i == -1;

      if(not cancel)
        m3u8 = download_m3u8(curl, m3u8.get_url(i).url);
    }

    // TODO: handle cancel

    assert(m3u8.is_playlist());
    assert(not m3u8.contains_relative_urls());

    //
    // 2. Download all video-parts in the m3u8-file.
    //

    curl.set_default_progressmeter();

    size_t ndigits = 1;
    while(std::pow(10, ndigits) < m3u8.get_urls().size())
      ndigits++;

    std::vector<std::tuple<std::filesystem::path, std::string>> pathurls = {};
    int i=1;
    for(auto url : m3u8.get_urls())
    {
      std::string segname = std::format("{}-{:0>{}}-v1-a1.ts", name, i, ndigits);
      //std::string segname = curl_wrapper::get_filename_from_url(url.url);
      pathurls.push_back(std::make_tuple(segname, url.url));
      i++;
    }

    auto results = curl.download_files(pathurls);
    //curl_wrapper::results_t results;
    //for(auto const& [part, url] : pathurls)
    //  results.succeeded_files.push_back(part);
    if(results.errors.size() > 0)
      throw results.errors;

    //
    // 3. Concat and convert all video-parts to mp4 via ffmpeg.
    //

    ret = concat_ffmpeg(name, results.succeeded_files);
  }
  catch(std::filesystem::filesystem_error const& error)
  {
    std::cerr << std::format("Error: {}!", error.what()) << std::endl;
    ret = -3;
  }
  catch(curl_wrapper_error const& error)
  {
    if(not error.filename().empty())
      std::cerr << std::format("Error: {} while downloading {}!", error.what(), error.filename()) << std::endl;
    else
      std::cerr << std::format("Error: {}!", error.what()) << std::endl;
    ret = -4;
  }
  catch(std::vector<curl_wrapper_error> const& errors)
  {
    for(auto const& error : errors)
      if(not error.filename().empty())
        std::cerr << std::format("Error: {} while downloading {}!", error.what(), error.filename()) << std::endl;
      else
        std::cerr << std::format("Error: {}!", error.what()) << std::endl;
    ret = -4;
  }
  catch(m3u8_errc const& error)
  {
    std::cerr << "Error: Url is not a m3u8-file!" << std::endl;
    ret = -5;
  }

  curl_wrapper::cleanup();

  return ret;
}

bool check_command(std::string const& cmd)
{
  return WEXITSTATUS(std::system((cmd + " > /dev/null 2>&1").c_str())) == 0;
}

auto download_m3u8(curl_wrapper const& curl, std::string const& url) -> m3u8_t
{
  auto result = curl.download_buffer(url);
  if(std::holds_alternative<curl_wrapper_error>(result))
    throw std::get<curl_wrapper_error>(result);

  assert(std::holds_alternative<std::vector<char>>(result));
  auto buffer = std::get<std::vector<char>>(result);

  //std::ranges::copy(buffer, std::ostream_iterator<char>(std::cout, ""));
  if(not is_m3u8(buffer))
    throw m3u8_errc::wrong_file_format;

  m3u8_t m3u8{buffer};
  if(m3u8.contains_relative_urls())
  {
    std::string const& host = get_baseurl(url);
    m3u8.set_baseurl(host);
  }

  return m3u8;
}

auto pick_playlist(m3u8_t const& m3u8) -> int
{
  std::cout << "is master" << std::endl;
  assert(m3u8.is_master());

  constexpr char CTRLC = 0x03;
  constexpr char CTRLD = 0x04;
  constexpr char ENTER = 0x0a;

  std::vector<char> keys{CTRLC, CTRLD, ENTER, 'c' /* cancel */};
  int n = 1;
  for(auto const& url : m3u8.get_urls())
  {
    std::string line = "";
    if(url.properties.contains("RESOLUTION"))
      line = url.properties.at("RESOLUTION");
    else
    {
      for(auto const& [key, value] : url.properties)
        line += key + "=" + value + " ";
    }
    assert(not line.empty());

    if(n == 1)
      line += " (default: 1)";

    std::cout << std::format("[{}]: {}", n, line) << std::endl;
    keys.push_back('0' + n); /* '0'+1 = '1', '0'+2 = '2', ... */

    n++;

    if(n == 10) // There are maximal 9 options.
      break;
  }

  int fails = 0;
  char key = 0;
  while(std::find(keys.cbegin(), keys.cend(), key) == keys.cend())
  {
    std::cout << std::format("Pick a playlist 1-{} (or press 'c' for cancel): ", n-1);
    key = getch();
    if(key != ENTER)
      std::cout << std::endl;
    //std::cout << std::endl << "0x" << std::hex << static_cast<int>(key) << std::endl;

    if(std::find(keys.cbegin(), keys.cend(), key) == keys.cend()) // invalid key
    {
      fails++;
      if(fails == 5)
        key = 'c';
    }
  }

  // Map key to correct index (or -1).
  int index = -1;
  if('1' <= key and key < '0' + n)
    index = key - '0' - 1; // map '1' -> 0, '2' -> 1, ...
  else if(key == ENTER)
    index = 0; // default
  else // key is 'c', CTRLC or CTRLD.
    index = -1;

  return index;
}

int concat_ffmpeg(std::string const& name, std::vector<std::filesystem::path> const& parts)
{
  std::filesystem::path const listfilename = name + "-list.txt";

  std::ofstream listfile{listfilename};
  for(auto part : parts)
  {
    if(listfile.fail())
      break;

    listfile << "file '" << part.c_str() << "'" << std::endl;
  }

  if(listfile.fail())
  {
    int const err = errno;
    std::error_code errc{err, std::generic_category()};
    throw std::filesystem::filesystem_error{"Couldn't write file", listfilename, errc};
  }

  // ---

  std::string const command = std::string{"ffmpeg -f concat -safe 0 -i "} + listfilename.c_str() + " " + name + ".mp4";
  int ret = WEXITSTATUS(std::system(command.c_str()));

  // Delete all intermediated files.
  std::remove(listfilename.c_str());
  for(auto part : parts)
    std::remove(part.c_str());

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
    fprintf(stderr, "Error: Can't open file `%s' for reading: %s\n", filename.c_str(), std::strerror(err));
    return std::vector<std::string> {};
  }
  // else

  size_t buffer_length = 256;
  char* buffer = static_cast<char*>( malloc(sizeof(char)*buffer_length) );

  ssize_t read_bytes = getline(&buffer, &buffer_length, m3u8);
  if(read_bytes == -1 and not feof(m3u8))
  {
    const int err = errno;
    fprintf(stderr, "Error: Reading from file `%s' failed: %s\n", filename.c_str(), std::strerror(err));
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
    fprintf(stderr, "Error: Reading from file `%s' failed: %s\n", filename.c_str(), std::strerror(err));
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
  std::cout <<
    "Usage: %s [-v|--verbose] <-n|--name NAME> <URL>\n"
    "Options:\n"
    "--help, -h      \t\tShow help.\n"
    "--vebose,-v     \t\tEnable verbose output.\n"
    "--name,-n <NAME>\t\t<NAME>.mp4 is the resulting filename.\n"
    "<URL>           \t\tUrl pointing to a m3u8-file.\n\n"
    "Download all the parts in a m3u8-file and concat them together via ffmpeg.\n"
    << std::endl;
}

auto parse_options(int argc, char* argv[]) -> std::optional<cmdline_t>
{
  cmdline_t cmdline;

  // Usage: <argv[0]> [--verbose|-v] --name NAME URL
  struct option long_options[] =
  {
    // long name, no_argument|required_argument, flag, val or nullptr
    {"help", no_argument, nullptr, 'h'},
    {"verbose", no_argument, nullptr, 'v'},
    {"name", required_argument, nullptr, 'n'},
    {nullptr, 0, nullptr, 0}
  };

  bool name_option = false;
  int parsed_options = 1; // argv[0] is the program name.

  int c = 0;
  int option_index = 0;
  while((c = getopt_long(argc, argv, "hvn:", long_options, &option_index)) != -1)
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
        cmdline.verbose_flag = true;
        parsed_options++;
        break;

      case 'h':
        cmdline.help_flag = true;
        parsed_options++;
        break;

      case 'n':
        name_option = true;
        cmdline.name = optarg;
        parsed_options += 2;
        break;

      case '?': // getopt_long printed an error-message.
      default:
        return {};
    }
  }

  if(cmdline.help_flag)
    return cmdline;

  cmdline.url = parsed_options < argc ? argv[parsed_options] : "";

  if(not name_option)
  {
    std::cerr << "Error: A name needs to be provided!" << std::endl;
    return {};
  }
  else if(parsed_options == argc) // Everything was parsed, so URL was forgotten.
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

/**
 * In Linux there is no conio.h with getch().
 * The internet says use getch() from ncurses or do it yourself.
 * As I don't want the ncurses dependency, I do it by hand.
 * (Properly not portable to Windows.)
 */
char getch(bool echo)
{
  struct termios old_termconf;
  tcgetattr(0, &old_termconf);

  struct termios new_termconf = old_termconf;
  new_termconf.c_lflag &= ~ICANON;              // disable buffered i/o */
  new_termconf.c_lflag &= echo ? ECHO : ~ECHO;  // echo mode
  tcsetattr(0, TCSANOW, &new_termconf);

  char ch = getchar();

  tcsetattr(0, TCSANOW, &old_termconf);

  return ch;
}

