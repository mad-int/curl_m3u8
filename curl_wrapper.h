#pragma once
#include <cassert>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

/**
 */
class curl_wrapper_error
{
  public:

    explicit curl_wrapper_error(std::string const& msg) : msg_(msg) {}
    virtual ~curl_wrapper_error() = default;

    curl_wrapper_error(curl_wrapper_error const&) = default;
    curl_wrapper_error& operator=(curl_wrapper_error const&) = default;

    virtual const char* what() const noexcept
    {
      return msg_.c_str();
    }

  private:

    std::string const msg_;
};


/**
 * Before using curl_wrapper call curl_wrapper::init()
 * and after usage call curl_wrapper::cleanup().
 */
class curl_wrapper
{
  public:

    typedef char byte_t;

    static void init();     // Call before curl_wrapper-usage (not thread-safe)!
    static void cleanup();  // Call after  curl_wrapper-usage (not thread-safe)!


  public:

    curl_wrapper()
      : curl_wrapper("curl_wrapper/0.1")
    {}
    explicit curl_wrapper(std::string const& useragent)
      : m_useragent(useragent)
    {}

    curl_wrapper(curl_wrapper const&) = default;
    curl_wrapper(curl_wrapper&&) = default;

    virtual ~curl_wrapper() = default;

    curl_wrapper& operator=(curl_wrapper const&) = default;
    curl_wrapper& operator=(curl_wrapper&&) = default;


  public:

    //! Synchronously download url to path.
    auto download_file(std::filesystem::path const& path, std::string const& url) const -> std::variant<std::filesystem::path, curl_wrapper_error>;

    //! Synchronously download url to a buffer.
    auto download_buffer(std::string const& url) const -> std::variant<std::vector<byte_t>, curl_wrapper_error>;

    static auto get_filename_from_url(std::string const& url) -> std::string;


  public:


    void useragent(std::string const& ua)
    {
      assert(not ua.empty());
      m_useragent = ua;
    }

    auto useragent() const -> std::string
    {
      return m_useragent;
    }

    void set_verbose()    { m_verbose_flag = true; }
    void clear_verbose()  { m_verbose_flag = false; }
    bool verbose() const  { return m_verbose_flag; }

    void set_default_progress_meter()   { m_default_progress_meter = true; }
    void clear_default_progress_meter() { m_default_progress_meter = false; }
    bool default_progress_meter() const { return m_default_progress_meter; }


  private:


    std::string m_useragent;
    bool m_verbose_flag = false;
    bool m_default_progress_meter = false;
};

