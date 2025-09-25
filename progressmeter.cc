// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#include <cassert>
#include <cmath>
#include <algorithm> // std::find_if
#include <format>
#include <optional>
#include <iostream> // std::cout, std::cerr
#include <vector>

#include <sys/ioctl.h>
#include <unistd.h> // STDOUT_FILENO

#include "progressmeter.h"
#include "string_util.h"

// Slightly overengineered: the multi-threading support is unnecessary for libcurl, I think, but whatever.

// ---

static const char* DEL_LINE = "\33[2K\r"; // Delete entire line and rewind.
static const char* CURSOR_UP = "\33[A";

using namespace std::chrono;
using namespace std::chrono_literals;

auto format_line(process_t const& process,
    int const length) -> std::string;
auto format_totalline(process_t const& main_process, size_t const finished, size_t const total,
    int const length) -> std::string;

auto format_line(std::string name, size_t transfered_bytes, std::optional<size_t> avg_speed,
    std::chrono::milliseconds const& duration, double percent, int const length) -> std::string;

static void transfered_list_push_back(process_t& process, size_t transfered);

// ---

download_process_t::download_process_t(int id, std::string const& name)
  : m_mutex{}, m_id{id}, m_process{name, std::chrono::system_clock::now()}
{
}

void download_process_t::update(size_t total, size_t transfered)
{
  std::lock_guard<std::mutex> guard{m_mutex};

  m_process.transfered = transfered;
  m_process.total = total;

  transfered_list_push_back(m_process, transfered);
}

auto download_process_t::copy() -> std::tuple<int, process_t>
{
  int id;
  process_t process;

  { // critical copy-section
    std::lock_guard<std::mutex> guard(m_mutex);

    id = m_id;
    process = m_process;
  }

  return std::make_tuple(id, process);
}

// ---

auto progressmeter_t::add_download(int id, std::string const& name) -> download_process_t*
{
  std::lock_guard<std::mutex> lock(m_mutex);

  [[maybe_unused]] auto has_id = [id](download_process_t const& p) { return p.get_id() == id; };
  assert(std::find_if(m_processes.begin(), m_processes.end(), has_id) == m_processes.end()
      and "a process with this id exists already");

  m_processes.emplace_back(id, name);
  if(m_finished + m_processes.size() > m_all)
  {
    assert(m_finished + m_processes.size() == m_all + 1);
    m_all++;
  }

  return &m_processes.back();
}

void progressmeter_t::remove_download(int id)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto has_id = [id](download_process_t const& p) { return p.get_id() == id; };
  [[maybe_unused]] auto it = std::find_if(m_processes.begin(), m_processes.end(), has_id);
  assert(it != m_processes.end() and "a process with this id doesn't exist");

  m_finished++;
  m_processes.erase(it);
}

void progressmeter_t::finish_download(int id)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto has_id = [id](download_process_t const& p) { return p.get_id() == id; };
  [[maybe_unused]] auto it = std::find_if(m_processes.begin(), m_processes.end(), has_id);
  assert(it != m_processes.end() and "a process with this id doesn't exist");

  it->finish();
}

void progressmeter_t::set_number_of_downloads(size_t n)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  if(n > m_all)
    m_all = n;
}

void progressmeter_t::print()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  using namespace std::chrono_literals;

  auto now = std::chrono::system_clock::now();

  int last_printed_lines = 0;

  // Copy main-process (m_main_process) and running-processes-list (m_processes).
  process_t main_process;
  std::list<std::tuple<int, process_t>> processes;
  {
    last_printed_lines = m_last_printed_lines;
    bool processes_finished = false;

    // copy m_main_process
    main_process = m_main_process;
    bool with_unknown_totals = false;

    // copy m_processes
    for(auto& process : m_processes)
    {
      processes.push_back(process.copy());
      auto const& [_, p] = processes.back();

      if(p.is_finished)
      {
        processes_finished = true;

        m_main_process.transfered += p.transfered;
        m_main_process.total += p.total;
      }

      // Note: m_main_process only contains the overall finished stats, see above.
      // The running stats need to be added extra.
      main_process.transfered += p.transfered;
      main_process.total += p.total;

      if(p.total == 0)
        with_unknown_totals = true;
    }

    transfered_list_push_back(main_process, main_process.transfered);

    // If one is unknown the overall total is unknown.
    if(with_unknown_totals)
      main_process.total = 0;

    // If processes finished we print the progressmeter
    // otherwise only if 1s past already.
    if(not processes_finished and now - m_last < 1s)
      return;
    m_last = now;
  }

  {
    auto running = std::partition(processes.begin(), processes.end(),
        [](auto const& p) { return std::get<1>(p).is_finished; });

    // Get terminal-window size.
    struct winsize w; // ws_row, ws_col
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    for(int i=0; i<last_printed_lines; i++)
      std::cout << CURSOR_UP << DEL_LINE;

    last_printed_lines = 0;

    // print finished processes
    for(auto finished = processes.begin(); finished != running; finished++)
    {
      auto const& [id, process] = *finished;
      std::cout << format_line(process, w.ws_col) << std::endl;

      m_finished++;
      m_processes.erase(std::find_if(m_processes.begin(), m_processes.end(),
            [id](download_process_t const& p) { return p.get_id() == id; }));
    }

    // print unfinished processes
    size_t running_processes = 0;
    for(; running != processes.end(); running++)
    {
      auto const& [_, process] = *running;

      std::cout << format_line(process, w.ws_col) << std::endl;
      running_processes++;

      last_printed_lines++;
    }

    // print total-line
    {
      std::cout << format_totalline(main_process, m_finished, m_all, w.ws_col) << std::endl;
      last_printed_lines++;
    }

    m_last_printed_lines = last_printed_lines;
  }
}

auto format_line(download_process_t& process, int const length) -> std::string
{
  return format_line(std::get<1>(process.copy()), length);
}

auto format_line(process_t const& process, int const length) -> std::string
{
  assert(process.total >= 0);

  auto const avg_speed = calc_avg_speed(process.transfered_list);

  using namespace std::chrono;
  milliseconds const duration = duration_cast<milliseconds>(system_clock::now() - process.start);

  double percent = -1.0;
  if(process.is_finished)
    percent = 1.0;
  else if(process.total > 0)
    percent = static_cast<double>(process.transfered)/static_cast<double>(process.total);

  return format_line(process.name, process.transfered, avg_speed, duration, percent, length);
}

/**
 * name         downloaded  speed  time      progress percent
 * total ( x/n      100 MB 5 MB/s 14:13 [########   ] 67%
 *
 * Everything element is overall e.g. overall transfered bytes, except for speed.
 * Speed is actual speed, not overall speed.
 */
auto format_totalline(process_t const& main_process, size_t const finished, size_t total,
    int const length) -> std::string
{
  assert(finished <= total);
  size_t const len = calc_numberlength(total);
  std::string const name = std::format("total ({0:{2}}/{1:{2}})", finished, total, len);

  auto const avg_speed = calc_avg_speed(main_process.transfered_list);

  using namespace std::chrono;
  milliseconds const duration = duration_cast<milliseconds>(system_clock::now() - main_process.start);

  double const percent = finished < total
    ? static_cast<double>(finished)/static_cast<double>(total)
    : 1.0;

  return format_line(name, main_process.transfered, avg_speed, duration, percent,
      length);
}

/**
 * name      downloaded     speed  time            progress percent
 * name       122,2 KiB 463 KiB/s 00:00 [#############    ] 100%
 */
auto format_line(std::string name, size_t transfered_bytes, std::optional<size_t> avg_speed,
    std::chrono::milliseconds const& duration, double percent, int const length) -> std::string
{
  using namespace std::chrono;

  assert(((0.0 <= percent) and (percent <= 1.0)) or (percent == -1.0));

  //  name      downloaded     speed    time   progress           percent
  //  variable                                 variable
  // [        ]  [       ]  [       ]  [   ]  [                 ] [  ]
  // name        122,2 KiB  463 KiB/s  00:00  [#############    ] 100%  <- pacman
  // total (x/n) ...
  //
  // name  percent downloaded    speed  estimated time until finished
  // name     100%      400MB  1.5MB/s       14:13 ETA                  <- scp

  // std::format("{:<20} {:5.1f} {}", process.m_name, transfered_quantity, transfered_unit);
  // {:<20} name: left-aligned 20 characters long (padded with space by default)
  // {:0>2} time: right-aligned 2 characters long (padded with 0)
  // {:5.1f} transfered in total(!) 5 character long from this 1 character after the decimal point
  // {:3} percent: left-aligned 3-characters long

  auto const [transfered_quantity, transfered_unit] = shorten_bytes(transfered_bytes);

  // already transfered
  // e.g. 122,2 KiB
  std::string const transfered_str = std::format("{:5.1f} {:>3}", transfered_quantity, transfered_unit);

  // time it took until now
  // e.g. 01:50
  auto const minutes = duration_cast<std::chrono::minutes>(duration);
  auto const seconds = duration_cast<std::chrono::seconds>(duration - minutes);
  std::string const time_str = std::format("{:0>2}:{:0>2}", minutes.count(), seconds.count());

  // transfer-speed
  // e.g. 463,0 KiB/s
  auto const [speed, speed_unit] = shorten_bytes(avg_speed.has_value() ? avg_speed.value() : 0.0);
  std::string const speed_str = avg_speed.has_value()
    ? std::format("{:5.1f} {:>5}", speed, speed_unit + "/s")
    : std::format(  "  -.- {:>5}", speed_unit + "/s");

  // percentage completed
  std::string percent_str = (percent != -1.0) ? std::format("{:3.0f}%", percent*100.0) : std::string{"---%"};
  if(percent >= 1.0)
    percent_str = "100%";

  // length without name and progess-bar (with padding whitespace in-between
  size_t length1 = 1 + transfered_str.length() + 2 + speed_str.length() + 1 + time_str.length() + 1 + percent_str.length();
  if(length1 + 20 > static_cast<size_t>(length)) // I want at least 20 characters for the name and the progress-bar.
    return "";
  // else

  size_t length2 = static_cast<size_t>(length) - length1; // space left for the name and the progress-bar

  // name
  std::string name_str = std::format("{: <{}}", shorten_string(name, length2/2 - 1), length2/2 - 1); // 1 is padding

  // progressbar
  int const barlength = length2/2 - 3; // 3 is for the one character padding, "[" and "]".

  std::string const progressbar_str = percent != -1.0
    ? std::string{"["} + calc_progressbar_filled(percent, barlength) + std::string{"]"}
    : std::string{"["} + calc_progressbar_undefined(seconds.count(), "<->", barlength) + std::string{"]"};

  return std::format(" {} {}  {} {} {} {}", name_str, transfered_str, speed_str, time_str, progressbar_str, percent_str);
}

auto calc_avg_speed(std::list<std::tuple<system_clock::time_point, size_t>> transfered_list) -> std::optional<size_t>
{
  // The first entry of the transfered_list it (start-time, 0),
  // so two entries are needed for calculating the avg. speed.
  if(transfered_list.size() >= 2)
  {
    auto const [last_time, last_transfered] = *(transfered_list.rbegin());
    auto const [before_last_time, before_last_transfered] = *(++transfered_list.rbegin());

    assert(last_time > before_last_time and "duration should be greater 0s (something around at least 1s)");
    assert(last_transfered >= before_last_transfered and "transfered bytes only grows");

    double const duration_last = std::chrono::duration<double>(last_time - before_last_time).count();
    size_t const transfered_diff = last_transfered - before_last_transfered;

    size_t avg_speed = static_cast<size_t>(static_cast<double>(transfered_diff) / duration_last);
    return avg_speed;
  }

  return {};
}

auto calc_progressbar_filled(double const percent, size_t const barlength) -> std::string
{
  assert((0.0 <= percent) and (percent <= 1.0));

  //double const percent = static_cast<double>(transfered)/static_cast<double>(total);
  size_t const filled = static_cast<size_t>(barlength * percent);

  std::string progressbar = std::format("{: <{}}", std::string(filled, '#'), barlength);
  assert(progressbar.length() == barlength);
  return progressbar;
}

auto calc_progressbar_undefined(size_t secs, std::string const& cursor, size_t barlength) -> std::string
{
  assert(cursor.length() < barlength);

  size_t const cursor_length = static_cast<int>(cursor.length());

  size_t pos = secs % (2*(barlength - cursor_length + 1));
  if(pos > barlength - cursor_length) // after barlength the cursor should go back (not jump at the begining!)
    pos = 2*(barlength - cursor_length) - pos + 1;
  size_t rightfill = barlength - pos - cursor_length;

  assert(0 <= pos and pos <= barlength - cursor_length);
  assert(0 <= rightfill and rightfill <= barlength - cursor_length);

  std::string progressbar = std::string(pos, ' ') + cursor + std::string(rightfill, ' ');
  assert(progressbar.length() == barlength);
  return progressbar;
}

//! Shorten bytes to a tuple of quantity (double) and unit ("B", "KB", "MB"or "GB).
//! The quantity has maximal 3 digits before the decimal point.
auto shorten_bytes(size_t const& bytes) -> std::tuple<double, std::string>
{
  size_t sbytes = bytes;
  double quantity = static_cast<double>(bytes);
  std::string unit = "B";

  if(sbytes >= 1000)
  {
    sbytes /= 1024;
    quantity = static_cast<double>(bytes) / 1024;
    unit = "KiB";
  }

  if(sbytes >= 1000)
  {
    sbytes /= 1024;
    quantity = static_cast<double>(bytes) / std::pow(1024,2);
    unit = "MiB";
  }

  if(sbytes >= 1000)
  {
    sbytes /= 1024;
    quantity = static_cast<double>(bytes) / std::pow(1024,3);
    unit = "GiB";
  }

  assert(sbytes < 1000 and "shorten bytes more is not supported");

  return std::make_tuple(quantity, unit);
}

auto shorten_string(std::string const& str, size_t const& maxlen) -> std::string
{
  if(str.size() <= maxlen)
    return str;

  std::string ret = str.substr(0, maxlen);
  if(ret.size() > 2)
  {
    ret[ret.size() - 1] = '.';
    ret[ret.size() - 2] = '.';
  }

  return ret;
}

void transfered_list_push_back(process_t& process, size_t transfered)
{
  auto const now = system_clock::now();

  if((now - std::get<0>(process.transfered_list.back())) > 1s)
  {
    process.transfered_list.push_back(std::make_tuple(now, transfered));
    while(process.transfered_list.size() > 5) // only keep the last 5 records
      process.transfered_list.pop_front();
  }
}

