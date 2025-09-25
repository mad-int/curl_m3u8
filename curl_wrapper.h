// GPL-3.0-or-later (see LICENSE or https://www.gnu.org/licenses/gpl-3.0.txt)
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

    explicit curl_wrapper_error(std::string const& msg, std::string const& url = "", std::string const& filename = "")
      : m_msg(msg), m_url(url), m_filename(filename)
    {}
    virtual ~curl_wrapper_error() = default;

    curl_wrapper_error(curl_wrapper_error const&) = default;
    curl_wrapper_error& operator=(curl_wrapper_error const&) = default;

    virtual const char* what() const noexcept
    {
      return m_msg.c_str();
    }

    virtual std::string url() const noexcept
    {
      return m_url;
    }

    virtual std::string filename() const noexcept
    {
      return m_filename;
    }

  private:

    std::string const m_msg;
    std::string const m_url;
    std::string const m_filename;
};

/**
 * Before using curl_wrapper call curl_wrapper::init()
 * and after usage call curl_wrapper::cleanup().
 */
class curl_wrapper
{
  public:

    static void init();     // Call before curl_wrapper-usage (not thread-safe)!
    static void cleanup();  // Call after  curl_wrapper-usage (not thread-safe)!

    using byte_t = char;
    using pathurl_t = std::tuple<std::filesystem::path, std::string>;

    struct results_t
    {
      std::vector<std::filesystem::path> succeeded_files;
      std::vector<curl_wrapper_error> errors;
    };

  public:

    curl_wrapper()
      : curl_wrapper("curl_wrapper/0.6")
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

    //! Download url to path.
    auto download_file(std::filesystem::path const& path, std::string const& url) const
      -> std::variant<std::filesystem::path, curl_wrapper_error>;

    //! Download url to a buffer.
    auto download_buffer(std::string const& url) const
      -> std::variant<std::vector<byte_t>, curl_wrapper_error>;

    //! Downloads a bunch of urls to paths.
    //! The order of files in the results can differ from pathurls, beside that errors can occurre.
    auto download_files(std::vector<pathurl_t> const pathurls) -> results_t;

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

    void set_default_progressmeter()   { m_default_progressmeter = true; }
    void clear_default_progressmeter() { m_default_progressmeter = false; }
    bool default_progressmeter() const { return m_default_progressmeter; }


  private:

    std::string m_useragent;
    bool m_verbose_flag = false;
    bool m_default_progressmeter = false;
};

