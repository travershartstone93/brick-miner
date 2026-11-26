#include "text_ops.hpp"
#include <QByteArray>
#include <iostream>
#include <type_traits>

int main(int argc, char **argv) {
  using Writer = void (*)(const std::vector<std::string> &, const char *);
  static_assert(std::is_same_v<decltype(&bricks::save_lines), Writer>);
  if (argc < 2)
    return 2;
  bricks::Strings records;
  for (int i = 2; i < argc; ++i) {
    QByteArray hex(argv[i]);
    auto raw = QByteArray::fromHex(hex);
    if (raw.toHex() != hex)
      return 2;
    records.push_back(raw.toStdString());
  }
  try {
    bricks::save_lines(records, argv[1]);
  } catch (const std::runtime_error &error) {
    std::cout << error.what() << '\n';
    return 1;
  }
}
