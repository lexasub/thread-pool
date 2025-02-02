#include "version.h"
namespace BS {
  std::string version::to_string() const {
    return std::to_string(major) + '.' + std::to_string(minor) + '.' +
           std::to_string(patch);
  }

  std::ostream &operator<<(std::ostream &stream, const version &ver) {
    stream << ver.to_string();
    return stream;
  }
}