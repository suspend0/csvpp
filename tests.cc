#include "csv.hpp"
#include <map>
#include <iostream>
#include <boost/noncopyable.hpp>

int errors = 0;

template <typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<T> &vec) {
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
std::ostream &operator<<(std::ostream &os, const std::map<T1, T2> &map) {
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

template <typename T, typename S>
static void EXPECT_TRUE(T &bool_conv, S &message) {
  if (bool_conv)
    return;
  ++errors;
  std::cout << "[ERROR] " << bool_conv << ":" << message << "\n";
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
  auto f = [&words](const std::string a, const std::string b) {
    words.push_back(a);
    words.push_back(b);
  };
  auto parser = csv::make_parser(f);
  parser.set_delim_char(' ');
  parser.Parse(csv_data);
  auto r = parser.Flush();
  EXPECT_TRUE(r, parser.ErrorString());

  std::vector<std::string> expected = {"hi",  "there", "how",
                                       "are", "you",   "doing"};
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
  parser.Parse(csv_data);
  auto r = parser.Flush();
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

static int free_func_total_a = 0;
static int free_func_total_b = 0;
void free_func(int a, int b) {
  free_func_total_a += a;
  free_func_total_b += b;
}
static void test_free_func() {
  auto parser = csv::make_parser(free_func);
  auto r = parser.ParseFile("test_numbers.csv");
  EXPECT_TRUE(r, parser.ErrorString());

  EXPECT_EQ(46, free_func_total_a);
  EXPECT_EQ(512, free_func_total_b);
}

#define run(fp)                           \
  std::cout << "[START] " << #fp << "\n"; \
  try {                                   \
    fp();                                 \
  }                                       \
  catch (const std::exception &e) {       \
    std::cerr << e.what() << "\n";        \
    throw;                                \
  }                                       \
  std::cout << "[END  ] " << #fp << "\n";

int main(int, char **) {
  run(test_number_file);
  run(test_grouping);
  run(test_spaces);
  run(test_functor);
  run(test_free_func);
  std::cout << (errors ? "ERRORS!\n" : "Ok\n");
  return errors;
}
