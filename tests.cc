#include "csv.hpp"
#include <map>
#include <iostream>
#include <boost/noncopyable.hpp>

int errors = 0;

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& vec) {
  os << "[";
  for (typename std::vector<T>::const_iterator it = vec.begin();
       it != vec.end();) {
    os << *it;
    for (++it; it != vec.end(); ++it) {
      os << "," << *it;
    }
  }
  os << "]";
  return os;
}
template <typename T1, typename T2>
std::ostream& operator<<(std::ostream& os, const std::map<T1, T2>& map) {
  os << "[";
  for (typename std::map<T1, T2>::const_iterator it = map.begin();
       it != map.end();) {
    os << it->first << ":" << it->second;
    for (++it; it != map.end(); ++it) {
      os << "," << it->first << ":" << it->second;
    }
  }
  os << "]";
  return os;
}

template <typename S>
static void EXPECT_TRUE(bool val, S& message) {
  if (val)
    return;
  ++errors;
  std::cout << "[ERROR] " << message << "\n";
}

template <typename T>
static void EXPECT_EQ(T expected, T actual) {
  if (expected == actual)
    return;
  ++errors;
  std::cout << "[ERROR] expected <" << expected << "> got <" << actual << ">\n";
}

static void test_spaces() {
  std::string csv_data = "hi there\nhow are\nyou doing\n";
  std::vector<std::string> words;
  auto f = [&words](const std::string& a, const std::string& b) {
    words.push_back(a);
    words.push_back(b);
  };
  auto parser = csv::make_parser(f);
  parser.set_delim_char(' ');
  auto r = parser.Parse(csv_data) && parser.Finish();
  EXPECT_TRUE(r, parser.ErrorString());

  std::vector<std::string> expected = {"hi",  "there", "how",
                                       "are", "you",   "doing"};
  EXPECT_EQ(expected, words);
}

static void test_header() {
  std::string csv_data = "name num\nlarry 1\nmary 3\n";
  std::map<std::string, uint32_t> values;
  auto f = [&values](const std::string& a, const uint32_t b) { values[a] = b; };
  auto parser = csv::make_parser(f);
  parser.set_skip_header();
  parser.set_delim_char(' ');
  parser.set_comment_mark("#");
  auto r = parser.Parse(csv_data) && parser.Finish();
  EXPECT_TRUE(r, parser.ErrorString());

  std::map<std::string, uint32_t> expected = {{"larry", 1}, {"mary", 3}};
  EXPECT_EQ(expected, values);
}

static void test_comments() {
  std::string csv_data = "hi there\n#how are\nyou doing\n";
  std::vector<std::string> words;
  auto f = [&words](const std::string& a, const std::string& b) {
    words.push_back(a);
    words.push_back(b);
  };
  auto parser = csv::make_parser(f);
  parser.set_delim_char(' ');
  parser.set_comment_mark("#");
  auto r = parser.Parse(csv_data) && parser.Finish();
  EXPECT_TRUE(r, parser.ErrorString());

  std::vector<std::string> expected = {"hi", "there", "you", "doing"};
  EXPECT_EQ(expected, words);
}

static void test_filter() {
  std::string csv_data = "hi there\nhow are\nyou doing\n";
  std::vector<std::string> words;
  auto f = [&words](const std::string& a, const std::string& b) {
    words.push_back(a);
    words.push_back(b);
  };
  auto parser = csv::make_parser(f);
  parser.set_delim_char(' ');
  parser.add_row_filter([](size_t, const char* buf, size_t len) {
    return len == 3 && std::equal(buf, buf + len, "how");
  });
  auto r = parser.Parse(csv_data) && parser.Finish();
  EXPECT_TRUE(r, parser.ErrorString());

  std::vector<std::string> expected = {"hi", "there", "you", "doing"};
  EXPECT_EQ(expected, words);
}

static void test_accept_filter() {
  std::string filter_term = "how";
  std::string csv_data = "hi there\nhow are\nyou doing\n";
  std::vector<std::string> words;
  auto f = [&words](const std::string& a, const std::string& b) {
    words.push_back(a);
    words.push_back(b);
  };
  auto parser = csv::make_parser(f);
  parser.set_delim_char(' ');
  parser.add_row_filter([filter_term](size_t field_num, const char* buf,
                                      size_t len) {
    if (field_num > 0) {
      return csv::ROW_OK;
    } else if (len == filter_term.size() &&
               std::equal(buf, buf + len, filter_term.data())) {
      return csv::ROW_OK;
    } else {
      return csv::ROW_DROP;
    }
  });
  auto r = parser.Parse(csv_data) && parser.Finish();
  EXPECT_TRUE(r, parser.ErrorString());

  std::vector<std::string> expected = {"how", "are"};
  EXPECT_EQ(expected, words);
}

static void test_quote_escaping() {
  std::string csv_data = "13,'Tiki,'\n14,'Let''s get busy'\n";
  std::map<int, std::string> words;
  auto f = [&words](const int& a, const std::string& b) { words[a] = b; };
  auto parser = csv::make_parser(f);
  parser.set_quote_char('\'');
  auto r = parser.Parse(csv_data) && parser.Finish();
  EXPECT_TRUE(r, parser.ErrorString());

  std::map<int, std::string> expected = {{13, "Tiki,"}, {14, "Let's get busy"}};
  EXPECT_EQ(expected, words);
}

static void test_grouping() {
  std::string csv_data =  //
      "6,joe\n"           //
      "3,louise\n"        //
      "2,mary\n"          //
      "1,louise\n";

  std::map<std::string, int> groups;
  auto parser = csv::make_parser(                           //
      [&groups](const int count, const std::string name) {  //
        groups[name] += count;                              //
      });
  auto r = parser.Parse(csv_data) && parser.Finish();
  EXPECT_TRUE(r, parser.ErrorString());

  std::map<std::string, int> expected{  //
      {"joe", 6},                       //
      {"mary", 2},                      //
      {"louise", 4},                    //
  };

  EXPECT_EQ(expected, groups);
}

static void test_number_file() {
  int tot_a = 0;
  int tot_b = 0;
  auto parser = csv::make_parser([&tot_a, &tot_b](int a, int b) {
    tot_a += a;
    tot_b += b;
  });
  auto r = parser.ParseFile("test_numbers.csv");
  EXPECT_TRUE(r, parser.ErrorString());

  EXPECT_EQ(46, tot_a);
  EXPECT_EQ(512, tot_b);
}

static void test_functor() {
  struct Adder : boost::noncopyable {
    int tot_a = 0;
    int tot_b = 0;
    void operator()(int a, int b) {
      tot_a += a;
      tot_b += b;
    }
  };
  Adder adder;
  auto parser = csv::make_parser(adder);
  auto r = parser.ParseFile("test_numbers.csv");
  EXPECT_TRUE(r, parser.ErrorString());

  EXPECT_EQ(46, adder.tot_a);
  EXPECT_EQ(512, adder.tot_b);
}

template <typename T>
void template_func(const std::string& path, T& command) {
  auto parser = csv::make_parser(command);
  auto r = parser.ParseFile(path);
  EXPECT_TRUE(r, parser.ErrorString());
}

static void test_template_func() {
  struct Adder : boost::noncopyable {
    int tot_a = 0;
    int tot_b = 0;
    void operator()(int a, int b) {
      tot_a += a;
      tot_b += b;
    }
  };
  Adder adder;
  template_func("test_numbers.csv", adder);

  EXPECT_EQ(46, adder.tot_a);
  EXPECT_EQ(512, adder.tot_b);
}

static int free_func_total_a = 0;
static int free_func_total_b = 0;
void free_func(int a, int b) {
  free_func_total_a += a;
  free_func_total_b += b;
}
static void test_free_func() {
  free_func_total_a = 0;
  free_func_total_b = 0;
  auto parser = csv::make_parser(free_func);
  auto r = parser.ParseFile("test_numbers.csv");
  EXPECT_TRUE(r, parser.ErrorString());

  EXPECT_EQ(46, free_func_total_a);
  EXPECT_EQ(512, free_func_total_b);
}
static void test_parse_stream() {
  free_func_total_a = 0;
  free_func_total_b = 0;
  std::istringstream input("1,2\n3,4\n");
  auto parser = csv::make_parser(free_func);
  auto r = parser.ParseStream(input);
  EXPECT_TRUE(r, parser.ErrorString());
  EXPECT_EQ(4, free_func_total_a);
  EXPECT_EQ(6, free_func_total_b);
}
static void test_bad_cast() {
  free_func_total_a = 0;
  free_func_total_b = 0;
  std::istringstream input("1,hi\n3,4\n");
  auto parser = csv::make_parser(free_func);
  parser.set_error_func([](size_t, size_t, const std::string&,
                           const std::exception_ptr) {
    return csv::ROW_DROP;  // squash stderr
  });
  auto r = parser.ParseStream(input);
  EXPECT_TRUE(r, parser.ErrorString());
  EXPECT_EQ(3, free_func_total_a);
  EXPECT_EQ(4, free_func_total_b);
}
static void test_bad_cast_callback() {
  free_func_total_a = 0;
  free_func_total_b = 0;
  std::istringstream input("1,hi\n3,4\n");
  auto parser = csv::make_parser(free_func);
  bool handler_called = false;
  parser.set_error_func([&handler_called](size_t row, size_t column,
                                          const std::string& message,
                                          const std::exception_ptr ex) {
    handler_called = true;
    EXPECT_EQ(size_t(1), row);
    EXPECT_EQ(size_t(2), column);
    EXPECT_TRUE(!message.empty(), "empty message");
    EXPECT_TRUE(bool(ex), "no exception");
    return csv::ROW_DROP;
  });
  auto r = parser.ParseStream(input);
  EXPECT_TRUE(r, parser.ErrorString());
  EXPECT_EQ(3, free_func_total_a);
  EXPECT_EQ(4, free_func_total_b);
  EXPECT_TRUE(handler_called, "handler not called");
}

#define run(fp)                           \
  std::cout << "[START] " << #fp << "\n"; \
  try {                                   \
    fp();                                 \
  }                                       \
  catch (const std::exception& e) {       \
    std::cerr << e.what() << "\n";        \
    throw;                                \
  }                                       \
  std::cout << "[END  ] " << #fp << "\n";

int main(int, char**) {
  run(test_number_file);
  run(test_grouping);
  run(test_spaces);
  run(test_header);
  run(test_comments);
  run(test_filter);
  run(test_accept_filter);
  run(test_quote_escaping);
  run(test_functor);
  run(test_template_func);
  run(test_free_func);
  run(test_parse_stream);
  run(test_bad_cast);
  run(test_bad_cast_callback);
  std::cout << (errors ? "ERRORS!\n" : "Ok\n");
  return errors;
}
