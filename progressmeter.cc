#include <cassert>
#include <cmath>
#include <algorithm> // std::find_if
#include <format>
#include <iostream> // std::cout, std::cerr
#include <vector>

#include <sys/ioctl.h>
#include <unistd.h> // STDOUT_FILENO

#include "progressmeter.h"

// Slightly overengineered: the multi-threading support is unnecessary, but whatever.

// ---

static const char* DEL_LINE = "\33[2K\r"; // Delete entire line and rewind.
static const char* CURSOR_UP = "\33[A";

using namespace std::chrono;
using namespace std::chrono_literals;

struct download_process_intern_t;
auto format_line(download_process_intern_t const& process, int const length) -> std::string;

// ---

download_process_t::download_process_t(int id, std::string const& name, time_point const& start)
  : m_id(id), m_name(name), m_start(start)
  , m_mutex(), m_transfered(0), m_total(0), m_finished(false)
  , m_transfered_list({std::make_tuple(system_clock::now(), 0)})
{
}

void download_process_t::update(size_t total, size_t transfered)
{
  std::lock_guard<std::mutex> guard{m_mutex};

  m_transfered = transfered;
  m_total = total;

  auto const now = system_clock::now();

  if((now - std::get<0>(m_transfered_list.back())) > 1s)
  {
    m_transfered_list.push_back(std::make_tuple(now, transfered));
    while(m_transfered_list.size() > 5) // only keep the last 5 records
      m_transfered_list.pop_front();
  }
}

// ---

struct download_process_intern_t
{
  using time_point = download_process_t::time_point;

  int id;
  std::string name;
  time_point start;

  size_t transfered;
  size_t total;
  bool finished;

  std::list<std::tuple<time_point, size_t>> transfered_list = {};

  static auto copy(download_process_t& process) -> download_process_intern_t;
};

auto download_process_intern_t::copy(download_process_t& process) -> download_process_intern_t
{
  std::lock_guard<std::mutex> guard(process.m_mutex);

  return download_process_intern_t{ process.m_id, process.m_name, process.m_start,
    process.m_transfered, process.m_total, process.m_finished, process.m_transfered_list};
}

// ---

auto progressmeter_t::add_download(int id, std::string const& name) -> download_process_t*
{
  std::lock_guard<std::mutex> lock(m_mutex);

  [[maybe_unused]] auto has_id = [id](download_process_t const& p) { return p.get_id() == id; };
  assert(std::find_if(m_processes.begin(), m_processes.end(), has_id) == m_processes.end()
      and "a process with this id exists already");

  m_processes.emplace_back(id, name);
  return &m_processes.back();
}

void progressmeter_t::remove_download(int id)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto has_id = [id](download_process_t const& p) { return p.get_id() == id; };
  [[maybe_unused]] auto it = std::find_if(m_processes.begin(), m_processes.end(), has_id);
  assert(it != m_processes.end() and "a process with this id doesn't exist");

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

void progressmeter_t::print()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  using namespace std::chrono_literals;

  auto now = std::chrono::system_clock::now();

  int last_printed_lines = 0;

  // copy process-list
  std::list<download_process_intern_t> processes;
  {
    last_printed_lines = m_last_printed_lines;
    bool processes_finished = false;

    // copy m_processes
    for(auto& process : m_processes)
    {
      processes.push_back( download_process_intern_t::copy(process) );
      if(processes.back().finished)
        processes_finished = true;
    }

    if(not processes_finished and now - m_last < 1s)
      return;
    m_last = now;
  }

  {
    auto running = std::partition(processes.begin(), processes.end(),
        [](download_process_intern_t const& p) { return p.finished; });

    // Get terminal-window size.
    struct winsize w; // ws_row, ws_col
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

    for(int i=0; i<last_printed_lines; i++)
      std::cout << CURSOR_UP << DEL_LINE;

    last_printed_lines = 0;

    // print finished processes
    for(auto finished = processes.begin(); finished != running; finished++)
    {
      std::cout << format_line(*finished, w.ws_col) << std::endl;

      auto id = finished->id;
      m_processes.erase(std::find_if(m_processes.begin(), m_processes.end(),
            [id](download_process_t const& p) { return p.get_id() == id; }));
    }

    // print unfinished processes
    for(; running != processes.end(); running++)
    {
      std::cout << format_line(*running, w.ws_col) << std::endl;
      last_printed_lines++;
    }

    m_last_printed_lines = last_printed_lines;
  }
}

auto format_line(download_process_t& process, int const length) -> std::string
{
  return format_line(download_process_intern_t::copy(process), length);
}

auto format_line(download_process_intern_t const& process, int const length) -> std::string
{
  using namespace std::chrono;

  //  name      downloaded     speed    time   progress        percent
  //  variable                                 variable
  // [        ]  [       ]  [       ]  [   ]  [                 ] [  ]
  // name        122,2 KiB  463 KiB/s  00:00  [#############    ] 100%  <- pacman
  // total (x/n) ...
  //
  // name  percent downloaded    speed  estimated time until finished
  // name     100%      400MB  1.5MB/s       14:13 ETA                  <- scp

  // std::format("{:<20} {:.1f} {}", process.m_name, transfered_quantity, transfered_unit);
  // {:<20} name: left-aligned 20 characters long (padded with space by default)
  // {:0>2} time: right-aligned 2 characters long (padded with 0)
  // {:5.1f} transfered in total(!) 5 character long from this 1 character after the decimal point
  // {:3} percent: left-aligned 3-characters long

  auto const now = system_clock::now();

  auto const [transfered_quantity, transfered_unit] = shorten_bytes(process.transfered);

  // already transfered
  // e.g. 122,2 KiB
  std::string const transfered_str = std::format("{:5.1f} {:>3}", transfered_quantity, transfered_unit);

  // time it took until now
  // e.g. 01:50
  auto const duration = now - process.start;
  auto const minutes = duration_cast<std::chrono::minutes>(duration);
  auto const seconds = duration_cast<std::chrono::seconds>(duration - minutes);
  std::string const time_str = std::format("{:0>2}:{:0>2}", minutes.count(), seconds.count());

  // transfer-speed
  // e.g. 463,0 KiB/s
  auto const [speed, speed_unit] = calc_avg_speed(process.transfered_list);
  std::string const speed_str = speed == -1.0
    ? std::format(  "  -.- {:>5}", speed_unit)
    : std::format("{:5.1f} {:>5}", speed, speed_unit);

  // percentage completed
  std::string percent_str = process.finished ? "100%" : "---%";
  if(process.total > 0)
  {
    assert(process.transfered <= process.total);
    double const percent = static_cast<double>(process.transfered)/static_cast<double>(process.total);
    percent_str = std::format("{:3.0f}%", percent*100.0);
  }
  // else

  // length without name and progess-bar (with padding whitespace in-between
  size_t length1 = 1 + transfered_str.length() + 2 + speed_str.length() + 1 + time_str.length() + 1 + percent_str.length();
  if(length1 + 20 > static_cast<size_t>(length)) // I want at least 20 characters for the name and the progress-bar.
    return "";
  // else

  size_t length2 = static_cast<size_t>(length) - length1; // space left for the name and the progress-bar

  // name
  std::string name = std::format("{: <{}}", shorten_string(process.name, length2/2 - 1), length2/2 - 1); // 1 is padding

  // progressbar
  std::string progressbar_str = "";
  int barlength = length2/2 - 3; // 3 is for the 1-character padding and "[]".
  if(process.total > 0)
  {
    progressbar_str = std::string{"["} + calc_progressbar_filled(process.transfered, process.total, barlength) + std::string{"]"};
  }
  else if(process.finished and process.total == 0)
  {
    progressbar_str = std::string{"["} + calc_progressbar_filled(process.transfered, process.transfered, barlength) + std::string{"]"};
  }
  else
  {
    size_t secs = std::chrono::duration_cast<std::chrono::seconds>(now - process.start).count();
    progressbar_str = std::string{"["} + calc_progressbar_undefined(secs, "<->", barlength) + std::string{"]"};
  }

  return std::format(" {} {}  {} {} {} {}", name, transfered_str, speed_str, time_str, progressbar_str, percent_str);
}

auto calc_avg_speed(std::list<std::tuple<system_clock::time_point, size_t>> transfered_list) -> std::tuple<double, std::string>
{
  bool const avg_speed_exists = (transfered_list.size() >= 2);

  // TODO: make it work also with a size() of 1 => assume duration is 1s from the start.

  size_t avg_speed = 0;
  if(avg_speed_exists)
  {
    auto const [last_time, last_transfered] = *(transfered_list.rbegin());
    auto const [before_last_time, before_last_transfered] = *(++transfered_list.rbegin());

    assert(last_time > before_last_time and "duration should be greater 0s (something around at least 1s)");
    assert(last_transfered >= before_last_transfered and "transfered bytes only grows");

    double const duration_last = std::chrono::duration<double>(last_time - before_last_time).count();
    size_t const transfered_diff = last_transfered - before_last_transfered;

    avg_speed = static_cast<size_t>(static_cast<double>(transfered_diff) / duration_last);
  }

  auto const [quantity, unit] = shorten_bytes(avg_speed_exists ? avg_speed : 0);
  return std::make_tuple(avg_speed_exists ? quantity : -1.0, unit+"/s");
}

auto calc_progressbar_filled(size_t const transfered, size_t const total, size_t const barlength) -> std::string
{
  assert(transfered <= total);

  double const percent = static_cast<double>(transfered)/static_cast<double>(total);
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

