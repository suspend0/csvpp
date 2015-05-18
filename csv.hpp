#include <csv.h>
#include <boost/lexical_cast.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

namespace csv {
struct Result {
  int number{0};
  std::string message{"success"};
  operator bool() { return number == 0; }
};
}
std::ostream& operator<<(std::ostream& os, const csv::Result& r) {
  os << r.message;
  return os;
}

namespace {
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
  csv::Result status;

 private:
  void set_error() {
    status.number = errno;
    status.message = strerror(errno);
  }
};

// generates sequence numbers used in template expansion
namespace helper {
template <int... Is>
struct index {};

template <int N, int... Is>
struct gen_seq : gen_seq<N - 1, N - 1, Is...> {};

template <int... Is>
struct gen_seq<0, Is...> : index<Is...> {};
}
}

/* A C++ wrapper around libcsv, see `make_parser` below */
namespace csv {
template <typename Lamda,                    //
          unsigned char delim_char_t = ',',  //
          unsigned char quote_char_t = '"'>
class CsvParser : public CsvParser<decltype(&Lamda::operator())> {
  using parent_type = CsvParser<decltype(&Lamda::operator())>;
  using this_type = CsvParser<Lamda>;

 public:
  CsvParser(Lamda func) : func(func) {
    parser.delim_char = delim_char_t;
    parser.quote_char = quote_char_t;
    csv_init(&parser, 0);
  }
  const Result& ParseFile(const std::string& filename) {
    MappedFile data(filename);
    if (!data) {
      return status_ = data.status;
    }
    Parse(data.begin(), data.end());
    Flush();
    return status_;
  }
  template <typename T>
  const Result& Parse(const T& str) {
    return Parse(str.data(), str.data() + str.length());
  }
  template <typename It>
  const Result& Parse(const It& begin, const It& end) {
    csv_parse(&parser, begin, end - begin, on_field, on_record, this);
    return update_status();
  }
  const Result& Flush() {
    csv_fini(&parser, on_field, on_record, this);
    return update_status();
  }
  const std::string& ErrorString() { return csv_error(&parser); }
  ~CsvParser() { csv_free(&parser); }
  operator bool() { return status_; }
  const Result& status() { return status_; }

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
  csv_parser parser;
  void accept_row() { parent_type::accept_row(func); }
  Lamda func;
  Result status_;

  const Result& update_status() {
    if (status_.number == 0) {
      status_.number = parser.status;
      status_.message = csv_error(&parser);
    }
    return status_;
  }
};
template <typename Type, typename R, typename... Args>
class CsvParser<R (Type::*)(Args...) const> {
  using mutator_t = std::function<void(const char* buf, size_t len)>;
  using values_t = std::tuple<Args...>;

 public:
  CsvParser() {
    setupFieldHandlers(typename helper::gen_seq<sizeof...(Args)>::index());
  }
  void accept_field(const char* buf, size_t len) {
    mutators.at(current_field)(buf, len);
    ++current_field;
  }
  template <typename F>
  void accept_row(F func) {
    current_field = 0;
    call_func(func, typename helper::gen_seq<sizeof...(Args)>::index());
  }
  template <typename F, int... S>
  void call_func(F func, helper::index<S...>) {
    func(std::get<S>(values)...);
  }

 private:
  values_t values;
  std::vector<mutator_t> mutators;
  size_t current_field = 0;

 private:
  template <int... S>
  void setupFieldHandlers(helper::index<S...>) {
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

template <typename F>
CsvParser<F> make_parser(F f) {
  return CsvParser<F>(f);
}
}  // namespace csv
