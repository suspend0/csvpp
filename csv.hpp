#include <csv.h>
#include <boost/lexical_cast.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

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
  void accept_field(const char* buf, size_t len) {
    mutators.at(current_field)(buf, len);
    ++current_field;
  }
  template <typename F>
  void accept_row(F& func) {
    current_field = 0;
    call_func(func,
              typename detail::sequence::generate<sizeof...(Args)>::index());
  }
  template <typename F, int... S>
  void call_func(F& func, detail::sequence::index<S...>) {
    func(std::get<S>(values)...);
  }

 private:
  std::tuple<Args...> values;
  std::vector<mutator_t> mutators;
  size_t current_field = 0;

 private:
  template <int... S>
  void setupFieldHandlers(detail::sequence::index<S...>) {
    setupFieldHandlers(std::get<S>(values)...);
  }
  template <typename F, typename... Fa>
  void setupFieldHandlers(F& arg, Fa&... args) {
    mutators.push_back([&arg](const char* buf, size_t len) {
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
template <typename F>
class CsvParser {
  using this_type = CsvParser<F>;

 public:
  CsvParser(const F& func) : func{func} { csv_init(&parser, 0); }
  ~CsvParser() { csv_free(&parser); }

  //
  void set_delim_char(unsigned char delim) { parser.delim_char = delim; }
  void set_quote_char(unsigned char quote) { parser.quote_char = quote; }
  void set_skip_header() { skip_next_row = true; }

  //
  bool ParseFile(const std::string& filename) {
    detail::MappedFile data(filename);
    if (!data) {
      return status = data.status;
    }
    return Parse(data.begin(), data.end()) && Flush();
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
  bool Flush() {
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
  const F& func;
  detail::Result status;
  bool skip_next_row{false};

 private:
  void accept_field(const char* buf, size_t len) {
    fields.accept_field(buf, len);
  }
  void accept_row() {
    if (skip_next_row) {
      skip_next_row = false;
    } else {
      fields.accept_row(func);
    }
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

}  // namespace csv
