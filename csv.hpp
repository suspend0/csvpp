#include <csv.h>
#include <iostream>
#include <boost/lexical_cast.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <typeinfo>

namespace csv {
namespace detail {

struct Result {
  int number{0};
  std::string message{"success"};
  operator bool() const { return number == 0; }
};
class MappedFile {
 private:
  int fd_;
  size_t size_;
  char* data_;

 public:
  MappedFile(const std::string& filename) : fd_(0), size_(0), data_(NULL) {
    fd_ = ::open(filename.c_str(), O_RDONLY);
    if (fd_ < 0) {
      set_error();
      return;
    }

    struct stat statbuf;
    if (::fstat(fd_, &statbuf) < 0) {
      set_error();
      return;
    }
    size_ = statbuf.st_size;

    data_ = (char*)::mmap(NULL, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
    if (data_ == NULL) {
      set_error();
      return;
    }
  }
  ~MappedFile() {
    ::munmap(data_, size_);
    ::close(fd_);
  }

  const char* begin() const { return data_; }
  const char* end() const { return data_ + size_; }

  operator bool() { return status; }
  detail::Result status;

 private:
  void set_error() {
    status.number = errno;
    status.message = strerror(errno);
  }
};

// generates sequence numbers used in template expansion
namespace sequence {
template <int... Is>
struct index {};

template <int N, int... Is>
struct generate : generate<N - 1, N - 1, Is...> {};

template <int... Is>
struct generate<0, Is...> : index<Is...> {};
}  // namespace sequence

namespace meta {

template <class F>
class fields;
// function pointer
template <class R, class... Args>
class fields<R (*)(Args...)> : public fields<R(Args...)> {};
// member function pointer
template <class C, class R, class... Args>
class fields<R (C::*)(Args...)> : public fields<R(Args...)> {};
// const member function pointer
template <class C, class R, class... Args>
class fields<R (C::*)(Args...) const> : public fields<R(Args...)> {};
// member object pointer
template <class C, class R>
class fields<R(C::*)> : public fields<R(C&)> {};
// functor
template <class F>
class fields : public fields<decltype(&F::operator())> {};
// reference
template <class F>
class fields<F&> : public fields<F> {};
// perfect reference
template <class F>
class fields<F&&> : public fields<F> {};
// impl
template <class R, class... Args>
class fields<R(Args...)> {
  using mutator_t = std::function<void(const char* buf, size_t len)>;

 public:
  fields() {
    setupFieldHandlers(
        typename detail::sequence::generate<sizeof...(Args)>::index());
  }
  void accept_field(size_t field_pos, const char* buf, size_t len) {
    if (field_pos < mutators.size()) {
      mutators[field_pos](buf, len);
    }
  }
  template <typename F>
  void accept_row(F& sink) {
    call_func(sink,
              typename detail::sequence::generate<sizeof...(Args)>::index());
  }
  template <typename F, int... S>
  void call_func(F& sink, detail::sequence::index<S...>) {
    sink(std::get<S>(values)...);
  }

 private:
  std::tuple<typename std::decay<Args>::type...> values;
  std::vector<mutator_t> mutators;

 private:
  template <int... S>
  void setupFieldHandlers(detail::sequence::index<S...>) {
    setupFieldHandlers(std::get<S>(values)...);
  }
  template <typename F, typename... Fa>
  void setupFieldHandlers(F& arg, Fa&... args) {
    size_t field_num = mutators.size();
    mutators.push_back([field_num, &arg](const char* buf, size_t len) {
      if (len > 0) {
        arg = boost::lexical_cast<F>(buf, len);
      } else {
        arg = F();
      }
    });
    setupFieldHandlers(args...);
  }
  void setupFieldHandlers() {
    // this is the terminal function for recursive template expansion
  }
};

}  // namespace meta
}  // detail

/* A C++ wrapper around libcsv, see `make_parser` below */
struct filter_result {
  bool drop;
  constexpr filter_result(bool b) : drop(b) {}
  operator bool() const { return drop; }
};
static constexpr filter_result ROW_DROP{true};
static constexpr filter_result ROW_OK{false};

template <typename F>
class CsvParser {
  using this_type = CsvParser<F>;

 public:
  // return true if field should cause row to be ignored
  using filter_function_type = std::function<
      filter_result(size_t field_num, const char* buf, size_t len)>;
  using error_callback_type = std::function<
      filter_result(size_t line_number, size_t field_number,
                    const std::string& error_message, std::exception_ptr ex)>;
  CsvParser(const F& sink) : sink{sink} { csv_init(&parser, 0); }
  ~CsvParser() { csv_free(&parser); }

  //
  void set_delim_char(unsigned char delim) { parser.delim_char = delim; }
  void set_quote_char(unsigned char quote) { parser.quote_char = quote; }
  void set_skip_header() { skip_row = true; }
  void set_error_func(const error_callback_type func) { error_func = func; }
  void set_comment_mark(const std::string& prefix) {
    auto is_comment = [prefix](size_t field_num, const char* buf, size_t len) {
      return field_num == 0 &&          //
             len >= prefix.length() &&  //
             std::equal(prefix.begin(), prefix.end(), buf);
    };
    return add_row_filter(is_comment);
  }
  /* Limitation: Fields are coerced to their types as they are
   * encountered, so these filters can't prevent conversion by
   * looking at data later in the same row. */
  void add_row_filter(const filter_function_type filter) {
    auto orig = filter_func;
    filter_func = [orig, filter](size_t field_num, const char* buf,
                                 size_t len) {
      return orig(field_num, buf, len) || filter(field_num, buf, len);
    };
  }

  //
  bool ParseFile(const std::string& filename) {
    detail::MappedFile data(filename);
    if (!data) {
      return status = data.status;
    }
    return Parse(data.begin(), data.end()) && Finish();
  }
  template <typename T>
  bool Parse(const T& str) {
    return Parse(str.data(), str.data() + str.length());
  }
  template <typename It>
  bool Parse(const It& begin, const It& end) {
    csv_parse(&parser, begin, end - begin, on_field, on_record, this);
    return update_status();
  }
  template <typename IoStream>
  bool ParseStream(IoStream& input) {
    char buf[4096];
    do {
      input.read(buf, sizeof(buf));
      csv_parse(&parser, buf, input.gcount(), on_field, on_record, this);
    } while (input && update_status());
    return Finish();
  }
  bool Finish() {
    csv_fini(&parser, on_field, on_record, this);
    return update_status();
  }
  const std::string& ErrorString() { return status.message; }
  operator bool() { return status; }

 private:
  static void on_field(void* data, size_t len, void* this_ptr) {
    this_type* t = reinterpret_cast<this_type*>(this_ptr);
    t->accept_field((char*)data, len);
  }
  static void on_record(int, void* this_ptr) {
    this_type* t = reinterpret_cast<this_type*>(this_ptr);
    t->accept_row();
  };

 private:
  detail::meta::fields<F> fields;
  csv_parser parser;
  const F& sink;
  detail::Result status;
  filter_function_type filter_func = [](size_t, const char*,
                                        size_t) { return ROW_OK; };
  error_callback_type error_func = [](size_t row, size_t column,
                                      const std::string& err,
                                      const std::exception_ptr) {
    std::cerr << "[csv.hpp] Exception at row " << row << ", column " << column
              << ": " << err << "\n";
    return ROW_DROP;
  };
  bool skip_row{false};
  size_t current_line = 0;
  size_t current_field = 0;

 private:
  void accept_field(const char* buf, size_t len) {
    skip_row = skip_row || filter_func(current_field, buf, len);
    if (!skip_row) {
      try {
        fields.accept_field(current_field, buf, len);
      }
      catch (std::exception& e) {
        skip_row = error_func(current_line + 1, current_field + 1, e.what(),
                              std::current_exception());
      }
    }
    ++current_field;
  }
  void accept_row() {
    if (!skip_row) {
      fields.accept_row(sink);
    } else {
      skip_row = false;
    }
    current_field = 0;
    ++current_line;
  }
  const detail::Result& update_status() {
    if (status.number == 0 && parser.status != 0) {
      status.number = parser.status;
      status.message = csv_error(&parser);
    }
    return status;
  }
};

template <typename F>
CsvParser<F> make_parser(F&& f) {
  return CsvParser<F>(f);
}

// used to ignore input fields
struct ignore {};

}  // namespace csv

namespace boost {
template <>
csv::ignore lexical_cast(const char*, size_t) {
  static csv::ignore instance;
  return instance;
}
}
