#include <cassert>
#include <cstring> // strerror, strncpy
#include <format>
#include <memory> // std::shared_ptr
#include <regex>
#include <string>

#include <iostream> // for debugging

#include "curl_wrapper.h"

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

// ---

//! Container-class for some elements that need to be initialised and cleaned up.
//! Helper so I don't need to deal with this in the curl_wrapper::download_*()-functions.
struct curl_handle_t
{
  curl_handle_t() = default;
  virtual ~curl_handle_t();

  curl_handle_t(curl_handle_t const&) = delete;
  curl_handle_t& operator=(curl_handle_t const&) = delete;

  bool init();
  bool init(std::filesystem::path const& path);

  inline auto get() const -> CURL* { return m_handle; }
  inline auto errormsg() const -> std::string { return std::string{m_errbuf}; }

  CURL* m_handle = nullptr;
  char* m_errbuf = nullptr;

  std::filesystem::path m_path = "";
  FILE* m_fh = nullptr;
};

struct curl_context_t
{
  std::string url;
  std::string useragent;
  bool verbose_flag;
  bool default_progress_meter;
};

using next_chunk_callback_t = size_t (*)(curl_wrapper::byte_t* ptr, size_t size, size_t nmemb, void* userdata);

static void curl_easy_setup(curl_handle_t const& handle, curl_context_t const& context,
    next_chunk_callback_t callback, void* userdata);

static auto append_file(curl_wrapper::byte_t* ptr, size_t size, size_t nmemb, void* userdata) -> size_t
{
  return fwrite(ptr, size, nmemb, reinterpret_cast<FILE*>(userdata));
}

static auto append_buffer(curl_wrapper::byte_t* ptr, size_t size, size_t nmemb, void* userdata) -> size_t
{
  auto buffer = reinterpret_cast<std::vector<curl_wrapper::byte_t>*>(userdata);
  size_t const size_old = buffer->size();
  buffer->reserve(buffer->size() + nmemb*size);
  buffer->insert(buffer->end(), ptr, ptr + nmemb*size);
  return buffer->size() - size_old;
}

auto curl_wrapper::download_file(std::filesystem::path const& path, std::string const& url) const
  -> std::variant<std::filesystem::path, curl_wrapper_error>
{
  assert(not path.empty());
  assert(not url.empty());

  curl_handle_t handle;
  bool success = handle.init(path);
  if(not success)
    return curl_wrapper_error(handle.errormsg());

  curl_context_t context {url, m_useragent, m_verbose_flag, m_default_progress_meter};

  curl_easy_setup(handle, context, append_file, handle.m_fh);
  CURLcode const res = curl_easy_perform(handle.get());
  if(res != CURLE_OK)
    return curl_wrapper_error{handle.errormsg()};
  // else
  return path;
}

//! For optimization make a head-request and retrieve the content length from it, then reserve this in the buffer.
//! See https://everything.curl.dev/libcurl-http/requests.html
//! and https://curl.se/libcurl/c/CURLINFO_CONTENT_LENGTH_DOWNLOAD_T.html
auto curl_wrapper::download_buffer(std::string const& url) const
  -> std::variant<std::vector<byte_t>, curl_wrapper_error>
{
  assert(not url.empty());
  std::vector<byte_t> buffer = {};

  curl_handle_t handle;
  bool success = handle.init();
  if(not success)
    return curl_wrapper_error(handle.errormsg());

  curl_context_t context {url, m_useragent, m_verbose_flag, m_default_progress_meter};

  curl_easy_setup(handle, context, append_buffer, &buffer);
  CURLcode const res = curl_easy_perform(handle.get());
  if(res != CURLE_OK)
    return curl_wrapper_error{handle.errormsg()};
  // else
  return buffer;
}

void curl_easy_setup(curl_handle_t const& handle, curl_context_t const& context,
    next_chunk_callback_t callback, void* userdata)
{
  assert(callback != nullptr);
  assert(userdata != nullptr);

  curl_easy_setopt(handle.get(), CURLOPT_URL,         context.url.c_str());
  curl_easy_setopt(handle.get(), CURLOPT_USERAGENT,   context.useragent.c_str());
  curl_easy_setopt(handle.get(), CURLOPT_VERBOSE,     context.verbose_flag ? 1 : 0);
  curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS,  context.default_progress_meter ? 0 : 1);

  // https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
  curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, userdata); // set userdata
  curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, callback);
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

bool curl_handle_t::init()
{
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

  return true;
}

bool curl_handle_t::init(std::filesystem::path const& path)
{
  bool success = init();
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

// ---

auto curl_wrapper::download_files(std::vector<std::tuple<std::filesystem::path, std::string>> const pathurls)
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
  //   curl_multi_perform(multi_handle, &still_running);
  //   curl_multi_timeout(multi_handle, &timeout);
  //   curl_multi_wait(multi_handle, &extra_fds, 1, timeout, &numfds);
  // }
  //

  int active_handles = 0;
  const int max_active_handles = 5;

  //curl_multi_setopt(multi_handle.get(), CURLMOPT_MAX_TOTAL_CONNECTIONS, max_active_handles);

  std::vector<curl_handle_t> handles(pathurls.size());

  size_t i = 0;

  // Run as long there are active handles or there are handles still waiting.
  while(active_handles > 0 or i<pathurls.size())
  {
    while(active_handles < max_active_handles and i<pathurls.size())
    {
      auto [path, url] = pathurls[i];
      std::cerr << i << ": Add `" << path.c_str() << "'" << std::endl;

      bool success = handles[i].init(path);
      if(not success)
      {
        std::string const errormsg = handles[i].errormsg();
        results.errors.push_back( curl_wrapper_error{errormsg, path.c_str()} );
        i++;

        continue;
      }
      // else

      curl_context_t context {url, m_useragent, m_verbose_flag, false};

      curl_easy_setup(handles[i], context, append_file, handles[i].m_fh);
      curl_easy_setopt(handles[i].get(), CURLOPT_PRIVATE, i);

      CURLMcode res = curl_multi_add_handle(multi_handle.get(), handles[i].get());
      if(res != CURLM_OK)
      {
        std::string const errormsg = curl_multi_strerror(res);
        results.errors.push_back( curl_wrapper_error{errormsg, path.c_str()} );
        i++;

        continue;
      }
      // else

      active_handles++;
      i++;
    }

    {
      // Note: Returns the number of currently active_handles.
      CURLMcode res = curl_multi_perform(multi_handle.get(), &active_handles);
      if(res != CURLM_OK)
      {
        std::string const errormsg = curl_multi_strerror(res);
        results.errors.push_back( curl_wrapper_error{errormsg} );
        return results;
      }
    }

    // Check for finished handles.
    int msgs_in_queue = 0;
    while(CURLMsg* m = curl_multi_info_read(multi_handle.get(), &msgs_in_queue))
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
        CURLcode res = curl_easy_getinfo(m->easy_handle, CURLINFO_PRIVATE, &index);
        assert(res == CURLE_OK);

        if(m->data.result == CURLE_OK)
        {
          results.succeeded_files.push_back(handles[index].m_path);
        }
        else // m->data.result != CURLE_OK
        {
          std::string const errormsg = curl_easy_strerror(m->data.result);
          results.errors.push_back( curl_wrapper_error{errormsg, handles[index].m_path.c_str()} );
        }

        curl_multi_remove_handle(multi_handle.get(), handles[index].get());
        std::cerr << "finished download: " << index << std::endl;
      }
      else // CURLMSG_NONE & _LAST is unused according to docu.
      {
        assert(false and "Unknown or unused CURLMsg");
      }
    }

    // Determine how long to wait before proceeding ...
    long timeout = 0;
    CURLMcode res = curl_multi_timeout(multi_handle.get(), &timeout);
    if(res != CURLM_OK)
    {
      std::string const errormsg = curl_multi_strerror(res);
      results.errors.push_back( curl_wrapper_error{errormsg} );
      return results;
    }

    // ... then wait.
    int numfds = 0;
    res = curl_multi_wait(multi_handle.get(), nullptr, 0, timeout, &numfds);
    if(res != CURLM_OK)
    {
      std::string const errormsg = curl_multi_strerror(res);
      results.errors.push_back( curl_wrapper_error{errormsg} );
      return results;
    }
  }

  handles.clear(); // Free all used handles.
  multi_handle.reset(); // Delete the multi_handle.

  return results;
}

// ---

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

