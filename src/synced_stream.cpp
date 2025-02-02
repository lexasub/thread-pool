#include "synced_stream.h"
#include <iostream>
#include <algorithm>
namespace BS {
  synced_stream::synced_stream() { add_stream(std::cout); }
  void synced_stream::add_stream(std::ostream &stream) {
    out_streams.push_back(&stream);
  }
  std::vector<std::ostream *> &synced_stream::get_streams() noexcept {
    return out_streams;
  }
  void synced_stream::remove_stream(std::ostream &stream) {
    out_streams.erase(
        std::remove(out_streams.begin(), out_streams.end(), &stream),
        out_streams.end());
  }
}