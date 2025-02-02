#include <iomanip>
#include <algorithm>
#include "arg_parser.h"
#include "synced_stream.h"
BS::synced_stream sync_out;
bool arg_parser::operator[](const std::string_view arg) {
  if (size() > 0)
    return (args.count(arg) == 1);
  return allowed[arg].def;
}

void arg_parser::add_argument(const std::string_view arg,
                              const std::string_view desc, const bool def) {
  allowed[arg] = {desc, def};
}
std::string_view arg_parser::get_executable() const { return executable; }
void arg_parser::show_help() const {
  int width = 1;
  for (const auto &[arg, opt] : allowed)
    width = std::max(width, static_cast<int>(arg.size()));
  sync_out.println("\nAvailable options (all are on/off and default to off):");
  for (const auto &[arg, opt] : allowed)
    sync_out.println("  ", std::left, std::setw(width), arg, "  ", opt.desc);
  sync_out.print("If no options are entered, the default is:\n  ");
  for (const auto &[arg, opt] : allowed) {
    if (opt.def)
      sync_out.print(arg, " ");
  }
  sync_out.println();
}
std::size_t arg_parser::size() const { return args.size(); }
bool arg_parser::verify() const {
  return std::all_of(
      args.begin(), args.end(),
      [this](const std::string_view arg) { return allowed.count(arg) == 1; });
}