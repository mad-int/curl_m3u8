// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

#include "progressmeter.h"

using namespace std::chrono_literals;

void run_download(download_process_t* download);

static std::atomic<size_t> finished = 0;

namespace
{
  std::random_device random;
  std::mt19937 generator(random());
}

int main()
{
  int const number_of_threads = 10;

  progressmeter_t progress;
  progress.set_number_of_downloads(number_of_threads);

  std::vector<download_process_t*> downloads = {};
  std::vector<std::thread> threads = {};

  finished = 0;

  for(int id=0; id<number_of_threads; id++)
  {
    auto download = progress.add_download(id, "file" + std::to_string(id));
    threads.push_back(std::thread(run_download, download));
  }

  while(finished < threads.size())
  {
    std::this_thread::sleep_for(1s);
    progress.print();
  }

  for(auto& thread : threads)
    thread.join();

  return 0;
}

void run_download(download_process_t* download)
{
  auto now = std::chrono::system_clock::now;

  std::uniform_int_distribution<size_t> distribution(1310548, 5*1310548);

  auto start = now();
  size_t const total = distribution(generator);

  while(now() - start < 500ms)
  {
    std::this_thread::sleep_for(20ms);
    download->update(0, 0);
  }

  auto last_update = now();

  int const chunksize = 7752*10;
  size_t transfered = 6836;

  std::this_thread::sleep_for(20ms);
  download->update(0, transfered);

  while(transfered < total)
  {
    std::this_thread::sleep_for(20ms);
    download->update(0, transfered);

    if(now() - last_update > 200ms)
    {
      last_update = now();
      transfered += chunksize;
    }
  }

  download->finish();

  finished++;
}

