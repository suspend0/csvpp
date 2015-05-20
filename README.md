
CSV++ is C++ Interface for libcsv
========

Dependencies
-------

 * A C++11 compiler
 * `libcsv` http://sourceforge.net/projects/libcsv/
 * `boost::lexical_cast` -- available on ubuntu as libboost-dev

Usage
-------
The reader is all fancy lambda type detection, so one just
defines a lamba matching the CSV file and off you go.

Let's assume you have a two-column CSV like the following:

    6,joe
    3,louise
    2,mary
    1,louise

You can parse it with following code:

    std::map<std::string, int> groups;
    auto parser = csv::make_parser(
      [&groups](const int count, const std::string name) {
        groups[name] += count;
      });
    parser.ParseFile("/path/to/file");

Or, with a functor

    class Grouper {
      std::map<std::string, int> groups;
      void operator()(const int count, const std::string name) {
        groups[name] += count;
      }
    };
    Grouper grouper;
    auto parser = csv::make_parser(grouper);
    parser.ParseFile("/path/to/file");

How it Works
-------
It inspects the provided lamba and builds a `std::tuple` with
the same types.  Each field is converted from a string to the
final type using `boost::lexical_cast`, and the provided lamda
is called once per row.

Performance
-------
Not really a goal of the library.  I don't think it does
anything crazy and should be fast enough.

Limitations
-------
 1. No real support for files with a variable number of columns,
    reading a subset of columns, etc.

