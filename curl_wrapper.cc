// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
#include <cassert>
#include <cstring> // strerror, strncpy
#include <format>
#include <fstream> // ifstream
#include <memory> // std::shared_ptr
#include <optional>
#include <regex>
#include <string>
#include <system_error> // std::error_code
#include <vector>

#include <iostream> // for debugging

#include "curl_wrapper.h"
#include "progressmeter.h"

#include <curl/curl.h>

// ---

// See Global preparation at https://curl.se/libcurl/c/libcurl-tutorial.html
void curl_wrapper::init()
{
  curl_global_init(CURL_GLOBAL_ALL);
}

void curl_wrapper::cleanup()
{
  curl_global_cleanup();
}

using byte_t = curl_wrapper::byte_t;
using pathurl_t = curl_wrapper::pathurl_t;

namespace
{
  struct curl_context_t;
  struct curl_handle_t;

  auto curl_multi_add_handle(CURLM* multi_handle, curl_context_t const& context, std::filesystem::path const& path,
      int index, download_process_t* process) -> std::variant<curl_handle_t, curl_wrapper_error>;
  auto curl_multi_handle_message(CURLM* multi_handle, CURLMsg* m) -> std::tuple<CURLcode, size_t>;

  auto verify_file(std::filesystem::path const& path, std::string const& url) -> std::optional<curl_wrapper_error>;

  void curl_easy_setup(CURL* handle, curl_context_t const& context, curl_write_callback callback, void* userdata);

  auto append_file(curl_wrapper::byte_t* ptr,   size_t size, size_t nmemb, void* userdata) -> size_t;
  auto append_buffer(curl_wrapper::byte_t* ptr, size_t size, size_t nmemb, void* userdata) -> size_t;

  int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

  // ---

  //! Container-class for some elements that need to be initialised and cleaned up.
  //! Helper so I don't need to deal with this in the curl_wrapper::download_*()-functions.
  struct curl_handle_t
  {
    curl_handle_t() = default;
    virtual ~curl_handle_t();

    curl_handle_t(curl_handle_t const&) = delete;
    auto operator=(curl_handle_t const&) -> curl_handle_t& = delete;

    curl_handle_t(curl_handle_t&&);
    curl_handle_t& operator=(curl_handle_t&&);

    bool init(std::string const& url);
    bool init(std::string const& url, std::filesystem::path const& path);

    void close();

    inline auto get() const -> CURL* { return m_handle; }
    inline auto errormsg() const -> std::string { return std::string{m_errbuf}; }

    CURL* m_handle = nullptr;
    char* m_errbuf = nullptr;

    std::string m_url = "";

    std::filesystem::path m_path = "";
    FILE* m_fh = nullptr;
  };

  struct curl_context_t
  {
    std::string url;
    std::string useragent;
    bool verbose_flag;
    bool default_progressmeter;
  };

  void curl_easy_setup(CURL* handle, curl_context_t const& context,
      curl_write_callback callback, void* userdata)
  {
    assert(callback != nullptr);
    assert(userdata != nullptr);

    if(context.verbose_flag)
      std::cout << std::format("Try to download: {}", context.url) << std::endl;

    curl_easy_setopt(handle, CURLOPT_URL,         context.url.c_str());
    curl_easy_setopt(handle, CURLOPT_USERAGENT,   context.useragent.c_str());
    curl_easy_setopt(handle, CURLOPT_VERBOSE,     context.verbose_flag ? 1 : 0);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS,  context.default_progressmeter ? 0 : 1);

    curl_off_t const maxrecv = 1*1'024*1'024; // max receive speed 1MB/s
    curl_easy_setopt(handle, CURLOPT_MAX_RECV_SPEED_LARGE, maxrecv);

    // https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, userdata); // set userdata
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, callback);
  }

  curl_handle_t::curl_handle_t(curl_handle_t&& other)
    : m_handle(other.m_handle), m_errbuf(other.m_errbuf), m_url(other.m_url), m_path(other.m_path), m_fh(other.m_fh)
  {
    other.m_handle = nullptr;
    other.m_errbuf = nullptr;
    other.m_url = "";
    other.m_path = "";
    other.m_fh = nullptr;
  }

  curl_handle_t::~curl_handle_t()
  {
    if(m_handle != nullptr)
      curl_easy_cleanup(m_handle);

    if(m_errbuf != nullptr)
      delete [] m_errbuf;

    if(m_fh != nullptr)
      fclose(m_fh);
  }

  void curl_handle_t::close()
  {
    if(m_fh != nullptr)
    {
      fclose(m_fh);
      m_fh = nullptr;
    }
  }

  auto curl_handle_t::operator=(curl_handle_t&& other) -> curl_handle_t&
  {
    std::swap(m_handle, other.m_handle);
    std::swap(m_errbuf, other.m_errbuf);
    std::swap(m_url, other.m_url);
    std::swap(m_path, other.m_path);
    std::swap(m_fh, other.m_fh);

    return *this;
  }

  bool curl_handle_t::init(std::string const& url)
  {
    m_url = url;

    m_errbuf = new char [CURL_ERROR_SIZE];
    if(m_errbuf == nullptr)
      return false;
    m_errbuf[0] = '\0';
    m_errbuf[CURL_ERROR_SIZE-1] = '\0';

    m_handle = curl_easy_init();
    if(m_handle == nullptr)
    {
      strncpy(m_errbuf, "Initialising curl handle failed", CURL_ERROR_SIZE-1);
      return false;
    }

    curl_easy_setopt(m_handle, CURLOPT_ERRORBUFFER, m_errbuf);

    return true;
  }

  bool curl_handle_t::init(std::string const& url, std::filesystem::path const& path)
  {
    bool success = init(url);
    if(not success)
      return false;

    m_fh = fopen(path.c_str(), "w");
    if(m_fh == nullptr)
    {
      int const err = errno;
      std::string const errormsg = std::format("Can't open file `{}' for writing: {}", path.c_str(), strerror(err));
      strncpy(m_errbuf, errormsg.c_str(), CURL_ERROR_SIZE-1);
      return false;
    }

    m_path = path;

    return true;
  }

} // namespace

// ---

auto curl_wrapper::download_file(std::filesystem::path const& path, std::string const& url) const
  -> std::variant<std::filesystem::path, curl_wrapper_error>
{
  assert(not path.empty());
  assert(not url.empty());

  curl_handle_t handle;
  bool success = handle.init(url, path);
  if(not success)
    return curl_wrapper_error(handle.errormsg());

  curl_context_t context {url, m_useragent, m_verbose_flag, m_default_progressmeter};

  curl_easy_setup(handle.get(), context, append_file, handle.m_fh);
  CURLcode const res = curl_easy_perform(handle.get());
  if(res != CURLE_OK)
    return curl_wrapper_error{handle.errormsg(), url, path};
  // else
  return path;
}

/**
 * For optimization make a head-request and retrieve the content length from it, then reserve this in the buffer.
 * See https://everything.curl.dev/libcurl-http/requests.html
 * and https://curl.se/libcurl/c/CURLINFO_CONTENT_LENGTH_DOWNLOAD_T.html
 * The content-length mustn't exist in the head!
 * Checking can be done via $ curl --head <URL>.
 */
auto curl_wrapper::download_buffer(std::string const& url) const
  -> std::variant<std::vector<byte_t>, curl_wrapper_error>
{
  assert(not url.empty());
  std::vector<byte_t> buffer = {};

  curl_handle_t handle;
  bool success = handle.init(url);
  if(not success)
    return curl_wrapper_error(handle.errormsg());

  curl_context_t context {url, m_useragent, m_verbose_flag, m_default_progressmeter};

  curl_easy_setup(handle.get(), context, append_buffer, &buffer);
  CURLcode const res = curl_easy_perform(handle.get());
  if(res != CURLE_OK)
    return curl_wrapper_error{handle.errormsg(), url};
  // else
  return buffer;
}

auto curl_wrapper::download_files(std::vector<pathurl_t> const pathurls)
  -> results_t
{
  results_t results;

  std::shared_ptr<CURLM> multi_handle {curl_multi_init(),
    [](CURLM* p) { curl_multi_cleanup(p); }};
  if(multi_handle.get() == nullptr)
  {
    results.errors.push_back(curl_wrapper_error{"Initialising curl multi-handle failed"});
    return results;
  }
  // else

  //
  // Download-Loop
  //
  // Inspired from example in https://curl.haxx.se/libcurl/c/curl_multi_wait.html
  // and https://github.com/curl/curl/issues/2996 with:
  // while (work_to_do)
  // {
  //   while(active_handles < max_active_handles)
  //     curl_multi_add_handle(multi_handle, easy_handle);
  //
  //   curl_multi_perform(multi_handle, &still_running);
  //   curl_multi_timeout(multi_handle, &timeout);
  //   curl_multi_wait(multi_handle, &extra_fds, 1, timeout, &numfds);
  // }
  //

  progressmeter_t progressmeter;
  progressmeter.set_number_of_downloads(pathurls.size());

  int active_handles = 0;
  const int max_active_handles = 5;

  //curl_multi_setopt(multi_handle.get(), CURLMOPT_MAX_TOTAL_CONNECTIONS, max_active_handles);

  std::vector<curl_handle_t> handles(pathurls.size());

  size_t i = 0;

  // Run as long there are active handles or there are handles still waiting.
  while(active_handles > 0 or i<pathurls.size())
  {
    // Make handles active (up to max_active_handles).
    while(active_handles < max_active_handles and i<pathurls.size())
    {
      auto [path, url] = pathurls[i];
      curl_context_t const context {url, m_useragent, m_verbose_flag, false};

      download_process_t* process = progressmeter.add_download(i, path);

      auto handle_error = curl_multi_add_handle(multi_handle.get(), context, path, i, process);
      if(std::holds_alternative<curl_handle_t>(handle_error))
      {
        handles[i] = std::move(std::get<curl_handle_t>(handle_error));
        active_handles++;
      }
      else
      {
        results.errors.push_back(std::get<curl_wrapper_error>(handle_error));
        progressmeter.remove_download(i);
      }

      i++;
    }

    // Note: Returns the number of currently active_handles.
    CURLMcode res = curl_multi_perform(multi_handle.get(), &active_handles);
    if(res != CURLM_OK)
    {
      results.errors.push_back(curl_wrapper_error{ curl_multi_strerror(res) } );
      return results;
    }

    // Handle messages.
    int consecutive_errors = 0;
    int msgs_in_queue = 0;
    while(CURLMsg* msg = curl_multi_info_read(multi_handle.get(), &msgs_in_queue))
    {
      auto [errorcode, index] = curl_multi_handle_message(multi_handle.get(), msg);

      curl_handle_t handle = std::move(handles[index]);
      std::string const url = handle.m_url;
      std::filesystem::path const path = handle.m_path;

      handle.close();

      // verify_file() is only possible after handle is close (and thus its file-handle written and closed).
      auto verify_error = errorcode == CURLE_OK ? verify_file(path, url) : std::optional<curl_wrapper_error>{};

      if(errorcode  == CURLE_OK and not verify_error.has_value()) // good case
      {
        consecutive_errors = 0;
        results.succeeded_files.push_back(path);
      }
      else if(errorcode  == CURLE_OK and verify_error.has_value()) // error case
      {
        consecutive_errors++;
        results.errors.push_back(verify_error.value());
      }
      else // errorcode != CURLE_OK // error case
      {
        consecutive_errors++;
        results.errors.push_back( curl_wrapper_error{curl_easy_strerror(errorcode), url, path} );
      }

      progressmeter.finish_download(index);

      // Break up after 5 consecutive errors.
      if(consecutive_errors >= 5)
        return results;
    }

    if(m_default_progressmeter)
      progressmeter.print();

    // Determine how long to wait before proceeding ...
    long timeout = 0;
    res = curl_multi_timeout(multi_handle.get(), &timeout);
    if(res != CURLM_OK)
    {
      results.errors.push_back( curl_wrapper_error{curl_multi_strerror(res)} );
      return results;
    }

    if(timeout == -1) // No timeout set. This happens!?
      timeout = 0;

    // ... then wait.
    int numfds = 0;
    res = curl_multi_wait(multi_handle.get(), nullptr, 0, timeout, &numfds);
    if(res != CURLM_OK)
    {
      results.errors.push_back( curl_wrapper_error{curl_multi_strerror(res)} );
      return results;
    }
  }

  return results;
}

namespace
{
  auto curl_multi_add_handle(CURLM* multi_handle, curl_context_t const& context, std::filesystem::path const& path,
      int index, download_process_t* process) -> std::variant<curl_handle_t, curl_wrapper_error>
  {
    curl_handle_t handle;
    bool success = handle.init(context.url, path);
    if(not success)
      return curl_wrapper_error{handle.errormsg(), context.url, path.c_str()};
    // else

    curl_easy_setup(handle.get(), context, append_file, handle.m_fh);
    curl_easy_setopt(handle.get(), CURLOPT_PRIVATE, index);

    // ---
    curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, 0);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, process);
    curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION, progress_callback);

    CURLMcode res = ::curl_multi_add_handle(multi_handle, handle.get());
    if(res != CURLM_OK)
      return curl_wrapper_error{curl_multi_strerror(res), context.url, path.c_str()};
    // else
    //

    return std::move(handle);
  }

  auto curl_multi_handle_message(CURLM* multi_handle, CURLMsg* m) -> std::tuple<CURLcode, size_t>
  {
    // struct CURLMsg
    // {
    //    CURLMSG msg;
    //    CURL* easy_handle;
    //    union
    //    {
    //      void* whatever;
    //      CURLCode result;
    //    } data;
    // }

    if(m->msg == CURLMSG_DONE)
    {
      // Move the handle out of the handles-vector.
      // When it gets out-of-scope the handles is cleaned up
      // and the corresponding file closed.
      //const size_t index = handle_index_map[m->easy_handle];

      size_t index;
      [[maybe_unused]] CURLcode res = curl_easy_getinfo(m->easy_handle, CURLINFO_PRIVATE, &index);
      assert(res == CURLE_OK);

      curl_multi_remove_handle(multi_handle, m->easy_handle);

      return std::make_tuple(m->data.result, index);
    }
    // else // CURLMSG_NONE & _LAST is unused according to docu.

    assert(false and "Unknown or unused CURLMsg");
    return std::make_tuple(CURLE_OK, -1);
  }

  auto verify_file(std::filesystem::path const& path, std::string const& url) -> std::optional<curl_wrapper_error>
  {
    std::error_code errc;
    auto const size = std::filesystem::file_size(path, errc);

    if(errc)
      return curl_wrapper_error{errc.message(), url, path};

    if(size <= 1'024) // 1 KB - too small something went wrong
    {
      // To small probably the server returned an error code like
      // > error code: 1015
      // (which is rate limit exceeded).
      // I got this sometimes.

      std::ifstream file{path};
      if(file.fail())
      {
        int const err = errno;
        errc = std::error_code{err, std::generic_category()};
        return curl_wrapper_error{std::format("Couldn't open file after download: {}", errc.message()), url, path};
      }

      // Maybe should just take the complete file-content instead of only a line.
      // Maybe only if it is <html> or text. Or only the html-part, I saw funny mixes.
      std::string line = "";
      while(std::getline(file, line))
      {
        std::smatch results;
        if(line.find("error code: 1015") != std::string::npos)
        {
          return curl_wrapper_error{"rate limit exceeded", url, path};
        }
        //else if(line.find("<title>") != std::string::npos)
        else if(std::regex_search(line, results, std::regex("<title>(.*)</title>")))
        {
          return curl_wrapper_error{results[1], url, path};
        }
      }

      return curl_wrapper_error{"unknown error", url, path};
    }

    return {};
  }

} // namespace

auto curl_wrapper::get_filename_from_url(std::string const& surl) -> std::string
{
  char* cpath;
  {
    CURLU *url = curl_url();
    curl_url_set(url, CURLUPART_URL, surl.c_str(), 0);

    CURLUcode const rc = curl_url_get(url, CURLUPART_PATH, &cpath, 0);

    curl_url_cleanup(url);

    if(rc != CURLUE_OK)
      return std::string {""};
    // else
  }

  std::string const path{cpath};
  curl_free(cpath);

  std::smatch match;
  if(not std::regex_match(path, match, std::regex {".*/([-\\w]+(.\\w+)?)$"}))
    return std::string {""};
  // else
  return match[1].str();
}

// ---

namespace
{
  //
  // Callbacks
  //

  auto append_file(curl_wrapper::byte_t* ptr, size_t size, size_t nmemb, void* userdata) -> size_t
  {
    return fwrite(ptr, size, nmemb, reinterpret_cast<FILE*>(userdata));
  }

  auto append_buffer(curl_wrapper::byte_t* ptr, size_t size, size_t nmemb, void* userdata) -> size_t
  {
    auto buffer = reinterpret_cast<std::vector<curl_wrapper::byte_t>*>(userdata);
    size_t const size_old = buffer->size();
    buffer->reserve(buffer->size() + nmemb*size);
    buffer->insert(buffer->end(), ptr, ptr + nmemb*size);
    return buffer->size() - size_old;
  }

  int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
  {
    download_process_t* process = static_cast<download_process_t*>(clientp);
    process->update(dltotal, dlnow);
    return 0;
  }
} // namespace

