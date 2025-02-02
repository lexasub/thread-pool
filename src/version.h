#ifndef VERSION_H
#define VERSION_H
#include <string>
#include <tuple>
#include <cstdint>
/**
 * @brief A struct used to store a version number, which can be checked and
 * compared at compilation time.
 */
namespace BS {
    struct version
    {
        constexpr version(const std::uint64_t major_, const std::uint64_t minor_, const std::uint64_t patch_) noexcept : major(major_), minor(minor_), patch(patch_) {}

        // In C++20 and later we can use the spaceship operator `<=>` to automatically
        // generate comparison operators. In C++17 we have to define them manually.
    #ifdef __cpp_impl_three_way_comparison
        std::strong_ordering operator<=>(const version&) const = default;
    #else
        [[nodiscard]] constexpr friend bool operator==(const version& lhs, const version& rhs) noexcept
        {
            return std::tuple(lhs.major, lhs.minor, lhs.patch) == std::tuple(rhs.major, rhs.minor, rhs.patch);
        }

        [[nodiscard]] constexpr friend bool operator!=(const version& lhs, const version& rhs) noexcept
        {
            return !(lhs == rhs);
        }

        [[nodiscard]] constexpr friend bool operator<(const version& lhs, const version& rhs) noexcept
        {
            return std::tuple(lhs.major, lhs.minor, lhs.patch) < std::tuple(rhs.major, rhs.minor, rhs.patch);
        }

        [[nodiscard]] constexpr friend bool operator>=(const version& lhs, const version& rhs) noexcept
        {
            return !(lhs < rhs);
        }

        [[nodiscard]] constexpr friend bool operator>(const version& lhs, const version& rhs) noexcept
        {
            return std::tuple(lhs.major, lhs.minor, lhs.patch) > std::tuple(rhs.major, rhs.minor, rhs.patch);
        }

        [[nodiscard]] constexpr friend bool operator<=(const version& lhs, const version& rhs) noexcept
        {
            return !(lhs > rhs);
        }
    #endif

        [[nodiscard]] std::string to_string() const;

        friend std::ostream &operator<<(std::ostream &stream, const version &ver);

        std::uint64_t major;
        std::uint64_t minor;
        std::uint64_t patch;
    }; // struct version
}
#endif //VERSION_H
