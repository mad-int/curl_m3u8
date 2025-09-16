#pragma once
#include <chrono>
#include <list>
#include <mutex>
#include <string>
#include <tuple>

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
  std::list<download_process_t> m_processes = {}; // currently running processes

  int m_last_printed_lines = 0;
  std::chrono::system_clock::time_point m_last = std::chrono::system_clock::now();
};

// ---

class download_process_t
{
public:

  using time_point = std::chrono::system_clock::time_point;


public:
  download_process_t(int id, std::string const& name, time_point const& start = std::chrono::system_clock::now());
  ~download_process_t() = default;

  download_process_t(download_process_t const& other) = delete;
  auto operator=(download_process_t const&) -> download_process_t& = delete;

  void update(size_t total, size_t now);

  inline int get_id() const { return m_id; }
  inline void finish()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_finished = true;
  }


private:

  friend class download_process_intern_t;

  // ---

  int const m_id;
  std::string const m_name;

  time_point const m_start = std::chrono::system_clock::now();

  // ---

  std::mutex m_mutex;

  size_t m_transfered;
  size_t m_total;
  bool m_finished;

  std::list<std::tuple<time_point, size_t>> m_transfered_list = {};
};

// Internal functions exposed for testing.
auto format_line(download_process_t& process, int const length) -> std::string;

auto calc_avg_speed(std::list<std::tuple<std::chrono::system_clock::time_point, size_t>> transfered_list)
  -> std::tuple<double, std::string>;
auto calc_progressbar_filled(size_t const transfered, size_t const total, size_t const barlength) -> std::string;
auto calc_progressbar_undefined(size_t secs, std::string const& cursor, size_t barlength) -> std::string;

auto shorten_bytes(size_t const& bytes) -> std::tuple<double, std::string>;
auto shorten_string(std::string const& str, size_t const& maxlen) -> std::string;

