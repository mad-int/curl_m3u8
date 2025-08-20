#include <cassert>
#include <cstring> // strerror, strncpy
#include <format>
#include <regex>
#include <string>

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

  inline auto get() const -> CURL* { return m_handle; }
  inline auto errormsg() const -> std::string { return std::string{m_errbuf}; }

  CURL* m_handle = nullptr;
  char* m_errbuf = nullptr;
};

struct curl_context_t
{
  std::string url;
  std::string useragent;
  bool verbose_flag;
  bool default_progress_meter;
};

using next_chunk_callback_t = size_t (*)(curl_wrapper::byte_t* ptr, size_t size, size_t nmemb, void* userdata);

static bool perform_download(curl_handle_t const& handle, curl_context_t const& context,
    next_chunk_callback_t callback, void* userdata);

static auto append_file(curl_wrapper::byte_t* ptr, size_t size, size_t nmemb, void* userdata) -> size_t
{
  return fwrite(ptr, size, nmemb, reinterpret_cast<FILE*>(userdata));
}

auto curl_wrapper::download_file(std::filesystem::path const& path, std::string const& url) const -> std::variant<std::filesystem::path, curl_wrapper_error>
{
  assert(not path.empty());
  assert(not url.empty());

  curl_handle_t handle;
  bool success = handle.init();
  if(not success)
    return curl_wrapper_error(handle.errormsg());

  FILE* fh = fopen(path.c_str(), "w");
  if(fh == nullptr)
  {
    int const err = errno;
    std::string const errormsg = std::format("Can't open file `{}' for writing: {}", path.c_str(), strerror(err));
    return curl_wrapper_error{errormsg};
  }

  curl_context_t context {url, m_useragent, m_verbose_flag, m_default_progress_meter};

  success = perform_download(handle, context, append_file, fh);

  fclose(fh);

  if(not success)
    return curl_wrapper_error{handle.errormsg()};
  // else
  return path;
}

static auto append_buffer(curl_wrapper::byte_t* ptr, size_t size, size_t nmemb, void* userdata) -> size_t
{
  auto buffer = reinterpret_cast<std::vector<curl_wrapper::byte_t>*>(userdata);
  size_t const size_old = buffer->size();
  buffer->reserve(buffer->size() + nmemb*size);
  buffer->insert(buffer->end(), ptr, ptr + nmemb*size);
  return buffer->size() - size_old;
}

//! For optimization make a head-request and retrieve the content length from it, then reserve this in the buffer.
//! See https://everything.curl.dev/libcurl-http/requests.html
//! and https://curl.se/libcurl/c/CURLINFO_CONTENT_LENGTH_DOWNLOAD_T.html
auto curl_wrapper::download_buffer(std::string const& url) const -> std::variant<std::vector<byte_t>, curl_wrapper_error>
{
  assert(not url.empty());
  std::vector<byte_t> buffer = {};

  curl_handle_t handle;
  bool success = handle.init();
  if(not success)
    return curl_wrapper_error(handle.errormsg());

  curl_context_t context {url, m_useragent, m_verbose_flag, m_default_progress_meter};

  success = perform_download(handle, context, append_buffer, &buffer);
  if(not success)
    return curl_wrapper_error{handle.errormsg()};
  // else
  return buffer;
}

bool perform_download(curl_handle_t const& handle, curl_context_t const& context,
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
  const CURLcode res = curl_easy_perform(handle.get());
  return res == CURLE_OK;
}

curl_handle_t::~curl_handle_t()
{
  if(m_handle != nullptr)
    curl_easy_cleanup(m_handle);

  if(m_errbuf != nullptr)
    delete [] m_errbuf;
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
    strncpy(m_errbuf, "Initialising cURL failed", CURL_ERROR_SIZE-1);
    return false;
  }

  return true;
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

