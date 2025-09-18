// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#pragma once
#include <chrono>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>

// ---

struct process_t
{
  using time_point = std::chrono::system_clock::time_point;

  std::string name;
  time_point start = std::chrono::system_clock::now();

  size_t transfered = 0;
  size_t total = 0;
  std::optional<size_t> avg_speed = {};

  bool is_finished = false;
};

// ---

class download_process_t;

class progressmeter_t
{
public:

  progressmeter_t() = default;
  ~progressmeter_t() = default;

  progressmeter_t(progressmeter_t const&) = delete;
  auto operator=(progressmeter_t const&) -> progressmeter_t& = delete;

  auto add_download(int id, std::string const& name) -> download_process_t*;
  void remove_download(int id);
  void finish_download(int id);

  void print();


private:

  std::mutex m_mutex;

  process_t m_main_process{"total"};
  size_t m_finished_processes = 0;
  size_t m_all_processes = 0;

  std::list<download_process_t> m_processes = {}; // currently running processes

  int m_last_printed_lines = 0;
  std::chrono::system_clock::time_point m_last = std::chrono::system_clock::now();
};

// ---

class download_process_t
{
public:

  friend class progressmeter_t;


public:

  download_process_t(int id, std::string const& name);
  ~download_process_t() = default;

  download_process_t(download_process_t const& other) = delete;
  auto operator=(download_process_t const&) -> download_process_t& = delete;

  void update(size_t total, size_t now);

  auto copy() -> std::tuple<int, process_t>;

  inline int get_id() const { return m_id; }
  inline void finish()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_process.is_finished = true;
  }


private:

  std::mutex m_mutex;

  int const m_id;
  process_t m_process;

  std::list<std::tuple<std::chrono::system_clock::time_point, size_t>> m_transfered_list = {};
};

// Internal functions exposed for testing.
auto format_line(download_process_t& process, int const length) -> std::string;

auto calc_avg_speed(std::list<std::tuple<std::chrono::system_clock::time_point, size_t>> transfered_list) -> std::optional<size_t>;
auto calc_progressbar_filled(double const percent, size_t const barlength) -> std::string;
auto calc_progressbar_undefined(size_t secs, std::string const& cursor, size_t barlength) -> std::string;

auto shorten_bytes(size_t const& bytes) -> std::tuple<double, std::string>;
auto shorten_string(std::string const& str, size_t const& maxlen) -> std::string;

