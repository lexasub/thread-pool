/**
 * ██████  ███████       ████████ ██   ██ ██████  ███████  █████  ██████          ██████   ██████   ██████  ██
 * ██   ██ ██      ██ ██    ██    ██   ██ ██   ██ ██      ██   ██ ██   ██         ██   ██ ██    ██ ██    ██ ██
 * ██████  ███████          ██    ███████ ██████  █████   ███████ ██   ██         ██████  ██    ██ ██    ██ ██
 * ██   ██      ██ ██ ██    ██    ██   ██ ██   ██ ██      ██   ██ ██   ██         ██      ██    ██ ██    ██ ██
 * ██████  ███████          ██    ██   ██ ██   ██ ███████ ██   ██ ██████  ███████ ██       ██████   ██████  ███████
 *
 * @file BS_thread_pool.hpp
 * @author Barak Shoshany (baraksh@gmail.com) (https://baraksh.com/)
 * @version 5.0.0
 * @date 2024-12-19
 * @copyright Copyright (c) 2024 Barak Shoshany. Licensed under the MIT license. If you found this project useful, please consider starring it on GitHub! If you use this library in software of any kind, please provide a link to the GitHub repository https://github.com/bshoshany/thread-pool in the source code and documentation. If you use this library in published research, please cite it as follows: Barak Shoshany, "A C++17 Thread Pool for High-Performance Scientific Computing", doi:10.1016/j.softx.2024.101687, SoftwareX 26 (2024) 101687, arXiv:2105.00613
 *
 * @brief `BS::thread_pool`: a fast, lightweight, modern, and easy-to-use C++17/C++20/C++23 thread pool library. This header file contains the entire library, and is the only file needed to use the library.
 */

#ifndef BS_THREAD_POOL_HPP
#define BS_THREAD_POOL_HPP

// We need to include <version> since if we're using `import std` it will not define any feature-test macros, including `__cpp_lib_modules`, which we need to check if `import std` is supported in the first place.
#ifdef __has_include
    #if __has_include(<version>)
        #include <version> // NOLINT(misc-include-cleaner)
    #endif
#endif

// If the macro `BS_THREAD_POOL_IMPORT_STD` is defined, import the C++ Standard Library as a module. Otherwise, include the relevant Standard Library header files. This is currently only officially supported by MSVC with Microsoft STL and LLVM Clang (NOT Apple Clang) with LLVM libc++. It is not supported by GCC with any standard library, or any compiler with GNU libstdc++. We also check that the feature is enabled by checking `__cpp_lib_modules`. However, MSVC defines this macro even in C++20 mode, which is not standards-compliant, so we check that we are in C++23 mode; MSVC currently reports `__cplusplus` as `202004L` for C++23 mode, so we use that value.
#if defined(BS_THREAD_POOL_IMPORT_STD) && defined(__cpp_lib_modules) && (__cplusplus >= 202004L) && (defined(_MSC_VER) || (defined(__clang__) && defined(_LIBCPP_VERSION) && !defined(__apple_build_version__)))
    // Only allow importing the `std` module if the library itself is imported as a module. If the library is included as a header file, this will force the program that included the header file to also import `std`, which is not desirable and can lead to compilation errors if the program `#include`s any Standard Library header files.
    #ifdef BS_THREAD_POOL_MODULE
import std;
    #else
        #error "The thread pool library cannot import the C++ Standard Library as a module using `import std` if the library itself is not imported as a module. Either use `import BS.thread_pool` to import the libary, or remove the `BS_THREAD_POOL_IMPORT_STD` macro. Aborting compilation."
    #endif
#else
    #undef BS_THREAD_POOL_IMPORT_STD

    #include <algorithm>
    #include <chrono>
    #include <condition_variable>
    #include <cstddef>
    #include <cstdint>
    #include <functional>
    #include <future>
    #include <iostream>
    #include <limits>
    #include <memory>
    #include <mutex>
    #include <optional>
    #include <queue>
    #include <string>
    #include <thread>
    #include <tuple>
    #include <type_traits>
    #include <utility>
    #include <variant>
    #include <vector>

    #ifdef __cpp_concepts
        #include <concepts>
    #endif
    #ifdef __cpp_exceptions
        #include <exception>
        #include <stdexcept>
    #endif
    #ifdef __cpp_impl_three_way_comparison
        #include <compare>
    #endif
    #ifdef __cpp_lib_int_pow2
        #include <bit>
    #endif
    #ifdef __cpp_lib_semaphore
        #include <semaphore>
    #endif
    #ifdef __cpp_lib_jthread
        #include <stop_token>
    #endif
#endif

#ifdef BS_THREAD_POOL_NATIVE_EXTENSIONS
    #if defined(_WIN32)
        #include <windows.h>
        #undef min
        #undef max
    #elif defined(__linux__) || defined(__APPLE__)
        #include <pthread.h>
        #include <sched.h>
        #include <sys/resource.h>
        #include <unistd.h>
        #if defined(__linux__)
            #include <sys/syscall.h>
            #include <sys/sysinfo.h>
        #endif
    #else
        #undef BS_THREAD_POOL_NATIVE_EXTENSIONS
    #endif
#endif

#if defined(__linux__)
    // On Linux, <sys/sysmacros.h> defines macros called `major` and `minor`. We undefine them here so the `version` struct can work.
    #ifdef major
        #undef major
    #endif
    #ifdef minor
        #undef minor
    #endif
#endif

/**
 * @brief A namespace used by Barak Shoshany's projects.
 */
#include "synced_stream.h"
#include "thread_pool.h"
namespace BS {
BS::synced_stream sync_out;
// Macros indicating the version of the thread pool library.
#define BS_THREAD_POOL_VERSION_MAJOR 5
#define BS_THREAD_POOL_VERSION_MINOR 0
#define BS_THREAD_POOL_VERSION_PATCH 0
#include "version.h"
/**
 * @brief The version of the thread pool library.
 */
inline constexpr version thread_pool_version(BS_THREAD_POOL_VERSION_MAJOR, BS_THREAD_POOL_VERSION_MINOR, BS_THREAD_POOL_VERSION_PATCH);

#ifdef BS_THREAD_POOL_MODULE
// If the library is being compiled as a module, ensure that the version of the module file matches the version of the header file.
static_assert(thread_pool_version == version(BS_THREAD_POOL_MODULE), "The versions of BS.thread_pool.cppm and BS_thread_pool.hpp do not match. Aborting compilation.");
/**
 * @brief A flag indicating whether the thread pool library was compiled as a C++20 module.
 */
inline constexpr bool thread_pool_module = true;
#else
/**
 * @brief A flag indicating whether the thread pool library was compiled as a C++20 module.
 */
inline constexpr bool thread_pool_module = false;
#endif

#ifdef BS_THREAD_POOL_IMPORT_STD
/**
 * @brief A flag indicating whether the thread pool library imported the C++23 Standard Library module using `import std`.
 */
inline constexpr bool thread_pool_import_std = true;
#else
/**
 * @brief A flag indicating whether the thread pool library imported the C++23 Standard Library module using `import std`.
 */
inline constexpr bool thread_pool_import_std = false;
#endif

#ifdef BS_THREAD_POOL_NATIVE_EXTENSIONS
/**
 * @brief A flag indicating whether the thread pool library's native extensions are enabled.
 */
inline constexpr bool thread_pool_native_extensions = true;
#else
/**
 * @brief A flag indicating whether the thread pool library's native extensions are enabled.
 */
inline constexpr bool thread_pool_native_extensions = false;
#endif

/**
 * @brief An enum containing some pre-defined priorities for convenience.
 */
enum pr : priority_t
{
    lowest = -128,
    low = -64,
    normal = 0,
    high = +64,
    highest = +127
};

#ifdef BS_THREAD_POOL_NATIVE_EXTENSIONS
    #if defined(_WIN32)
/**
 * @brief An enum containing pre-defined OS-specific process priority values for portability.
 */
enum class os_process_priority
{
    idle = IDLE_PRIORITY_CLASS,
    below_normal = BELOW_NORMAL_PRIORITY_CLASS,
    normal = NORMAL_PRIORITY_CLASS,
    above_normal = ABOVE_NORMAL_PRIORITY_CLASS,
    high = HIGH_PRIORITY_CLASS,
    realtime = REALTIME_PRIORITY_CLASS
};

/**
 * @brief An enum containing pre-defined OS-specific thread priority values for portability.
 */
enum class os_thread_priority
{
    idle = THREAD_PRIORITY_IDLE,
    lowest = THREAD_PRIORITY_LOWEST,
    below_normal = THREAD_PRIORITY_BELOW_NORMAL,
    normal = THREAD_PRIORITY_NORMAL,
    above_normal = THREAD_PRIORITY_ABOVE_NORMAL,
    highest = THREAD_PRIORITY_HIGHEST,
    realtime = THREAD_PRIORITY_TIME_CRITICAL
};
    #elif defined(__linux__) || defined(__APPLE__)
/**
 * @brief An enum containing pre-defined OS-specific process priority values for portability.
 */
enum class os_process_priority
{
    idle = PRIO_MAX - 2,
    below_normal = PRIO_MAX / 2,
    normal = 0,
    above_normal = PRIO_MIN / 3,
    high = PRIO_MIN * 2 / 3,
    realtime = PRIO_MIN
};

/**
 * @brief An enum containing pre-defined OS-specific thread priority values for portability.
 */
enum class os_thread_priority
{
    idle,
    lowest,
    below_normal,
    normal,
    above_normal,
    highest,
    realtime
};
    #endif

/**
 * @brief Get the processor affinity of the current process using the current platform's native API. This should work on Windows and Linux, but is not possible on macOS as the native API does not allow it.
 *
 * @return An `std::optional` object, optionally containing the processor affinity of the current process as an `std::vector<bool>` where each element corresponds to a logical processor. If the returned object does not contain a value, then the affinity could not be determined. On macOS, this function always returns `std::nullopt`.
 */
[[nodiscard]] inline std::optional<std::vector<bool>> get_os_process_affinity()
{
    #if defined(_WIN32)
    DWORD_PTR process_mask = 0;
    DWORD_PTR system_mask = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) == 0)
        return std::nullopt;
        #ifdef __cpp_lib_int_pow2
    const std::size_t num_cpus = static_cast<std::size_t>(std::bit_width(system_mask));
        #else
    std::size_t num_cpus = 0;
    if (system_mask != 0)
    {
        num_cpus = 1;
        while ((system_mask >>= 1U) != 0U)
            ++num_cpus;
    }
        #endif
    std::vector<bool> affinity(num_cpus);
    for (std::size_t i = 0; i < num_cpus; ++i)
        affinity[i] = ((process_mask & (1ULL << i)) != 0ULL);
    return affinity;
    #elif defined(__linux__)
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    if (sched_getaffinity(getpid(), sizeof(cpu_set_t), &cpu_set) != 0)
        return std::nullopt;
    const int num_cpus = get_nprocs();
    if (num_cpus < 1)
        return std::nullopt;
    std::vector<bool> affinity(static_cast<std::size_t>(num_cpus));
    for (std::size_t i = 0; i < affinity.size(); ++i)
        affinity[i] = CPU_ISSET(i, &cpu_set);
    return affinity;
    #elif defined(__APPLE__)
    return std::nullopt;
    #endif
}

/**
 * @brief Set the processor affinity of the current process using the current platform's native API. This should work on Windows and Linux, but is not possible on macOS as the native API does not allow it.
 *
 * @param affinity The processor affinity to set, as an `std::vector<bool>` where each element corresponds to a logical processor.
 * @return `true` if the affinity was set successfully, `false` otherwise. On macOS, this function always returns `false`.
 */
inline bool set_os_process_affinity(const std::vector<bool>& affinity)
{
    #if defined(_WIN32)
    DWORD_PTR process_mask = 0;
    for (std::size_t i = 0; i < std::min<std::size_t>(affinity.size(), sizeof(DWORD_PTR) * 8); ++i)
        process_mask |= (affinity[i] ? (1ULL << i) : 0ULL);
    return SetProcessAffinityMask(GetCurrentProcess(), process_mask) != 0;
    #elif defined(__linux__)
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);
    for (std::size_t i = 0; i < std::min<std::size_t>(affinity.size(), CPU_SETSIZE); ++i)
    {
        if (affinity[i])
            CPU_SET(i, &cpu_set);
    }
    return sched_setaffinity(getpid(), sizeof(cpu_set_t), &cpu_set) == 0;
    #elif defined(__APPLE__)
    return affinity[0] && false; // NOLINT(readability-simplify-boolean-expr) // Using `affinity` to suppress unused parameter warning.
    #endif
}

/**
 * @brief Get the priority of the current process using the current platform's native API. This should work on Windows, Linux, and macOS.
 *
 * @return An `std::optional` object, optionally containing the priority of the current process, as a member of the enum `BS::os_process_priority`. If the returned object does not contain a value, then either the priority could not be determined, or it is not one of the pre-defined values and therefore cannot be represented in a portable way.
 */
[[nodiscard]] inline std::optional<os_process_priority> get_os_process_priority()
{
    #if defined(_WIN32)
    // On Windows, this is straightforward.
    const DWORD priority = GetPriorityClass(GetCurrentProcess());
    if (priority == 0)
        return std::nullopt;
    return static_cast<os_process_priority>(priority);
    #elif defined(__linux__) || defined(__APPLE__)
    // On Linux/macOS there is no direct analogue of `GetPriorityClass()` on Windows, so instead we get the "nice" value. The usual range is -20 to 19 or 20, with higher values corresponding to lower priorities. However, we are only using 6 pre-defined values for portability, so if the value was set via any means other than `BS::set_os_process_priority()`, it may not match one of our pre-defined values. Note that `getpriority()` returns -1 on error, but since this does not correspond to any of our pre-defined values, this function will return `std::nullopt` anyway.
    const int nice_val = getpriority(PRIO_PROCESS, static_cast<id_t>(getpid()));
    switch (nice_val)
    {
    case static_cast<int>(os_process_priority::idle):
        return os_process_priority::idle;
    case static_cast<int>(os_process_priority::below_normal):
        return os_process_priority::below_normal;
    case static_cast<int>(os_process_priority::normal):
        return os_process_priority::normal;
    case static_cast<int>(os_process_priority::above_normal):
        return os_process_priority::above_normal;
    case static_cast<int>(os_process_priority::high):
        return os_process_priority::high;
    case static_cast<int>(os_process_priority::realtime):
        return os_process_priority::realtime;
    default:
        return std::nullopt;
    }
    #endif
}

/**
 * @brief Set the priority of the current process using the current platform's native API. This should work on Windows, Linux, and macOS. However, note that higher priorities might require elevated permissions.
 *
 * @param priority The priority to set. Must be a value from the enum `BS::os_process_priority`.
 * @return `true` if the priority was set successfully, `false` otherwise. Usually, `false` means that the user does not have the necessary permissions to set the desired priority.
 */
inline bool set_os_process_priority(const os_process_priority priority)
{
    #if defined(_WIN32)
    // On Windows, this is straightforward.
    return SetPriorityClass(GetCurrentProcess(), static_cast<DWORD>(priority)) != 0;
    #elif defined(__linux__) || defined(__APPLE__)
    // On Linux/macOS there is no direct analogue of `SetPriorityClass()` on Windows, so instead we set the "nice" value. The usual range is -20 to 19 or 20, with higher values corresponding to lower priorities. However, we are only using 6 pre-defined values for portability. Note that the "nice" values are only relevant for the `SCHED_OTHER` policy, but we do not set that policy here, as it is per-thread rather than per-process.
    // Also, it's important to note that a non-root user cannot decrease the nice value (i.e. increase the process priority), only increase it. This can cause confusing behavior. For example, if the current priority is `BS::os_process_priority::normal` and the user sets it to `BS::os_process_priority::idle`, they cannot change it back `BS::os_process_priority::normal`.
    return setpriority(PRIO_PROCESS, static_cast<id_t>(getpid()), static_cast<int>(priority)) == 0;
    #endif
}
#endif

/**
 * @brief A fast, lightweight, modern, and easy-to-use C++17/C++20/C++23 thread pool class. This alias defines a thread pool with all optional features disabled.
 */
using light_thread_pool = thread_pool<tp::none>;

/**
 * @brief A fast, lightweight, modern, and easy-to-use C++17/C++20/C++23 thread pool class. This alias defines a thread pool with task priority enabled.
 */
using priority_thread_pool = thread_pool<tp::priority>;

/**
 * @brief A fast, lightweight, modern, and easy-to-use C++17/C++20/C++23 thread pool class. This alias defines a thread pool with pausing enabled.
 */
using pause_thread_pool = thread_pool<tp::pause>;

/**
 * @brief A fast, lightweight, modern, and easy-to-use C++17/C++20/C++23 thread pool class. This alias defines a thread pool with wait deadlock checks enabled.
 */
using wdc_thread_pool = thread_pool<tp::wait_deadlock_checks>;

#ifdef __cpp_lib_semaphore
using binary_semaphore = std::binary_semaphore;
template <std::ptrdiff_t LeastMaxValue = std::counting_semaphore<>::max()>
using counting_semaphore = std::counting_semaphore<LeastMaxValue>;
#else
/**
 * @brief A polyfill for `std::counting_semaphore`, to be used if C++20 features are not available. A `counting_semaphore` is a synchronization primitive that allows more than one concurrent access to the same resource. The number of concurrent accessors is limited by the semaphore's counter, which is decremented when a thread acquires the semaphore and incremented when a thread releases the semaphore. If the counter is zero, a thread trying to acquire the semaphore will be blocked until another thread releases the semaphore.
 *
 * @tparam LeastMaxValue The least maximum value of the counter. (In this implementation, it is also the actual maximum value.)
 */
template <std::ptrdiff_t LeastMaxValue = std::numeric_limits<std::ptrdiff_t>::max()>
class [[nodiscard]] counting_semaphore
{
    static_assert(LeastMaxValue >= 0, "The least maximum value for a counting semaphore must not be negative.");

public:
    /**
     * @brief Construct a new counting semaphore with the given initial counter value.
     *
     * @param desired The initial counter value.
     */
    constexpr explicit counting_semaphore(const std::ptrdiff_t desired) : counter(desired) {}

    // The copy and move constructors and assignment operators are deleted. The semaphore cannot be copied or moved.
    counting_semaphore(const counting_semaphore&) = delete;
    counting_semaphore(counting_semaphore&&) = delete;
    counting_semaphore& operator=(const counting_semaphore&) = delete;
    counting_semaphore& operator=(counting_semaphore&&) = delete;
    ~counting_semaphore() = default;

    /**
     * @brief Returns the internal counter's maximum possible value, which in this implementation is equal to `LeastMaxValue`.
     *
     * @return The internal counter's maximum possible value.
     */
    [[nodiscard]] static constexpr std::ptrdiff_t max() noexcept
    {
        return LeastMaxValue;
    }

    /**
     * @brief Atomically decrements the internal counter by 1 if it is greater than 0; otherwise blocks until it is greater than 0 and can successfully decrement the internal counter.
     */
    void acquire()
    {
        std::unique_lock lock(mutex);
        cv.wait(lock,
            [this]
            {
                return counter > 0;
            });
        --counter;
    }

    /**
     * @brief Atomically increments the internal counter. Any thread(s) waiting for the counter to be greater than 0, such as due to being blocked in `acquire()`, will subsequently be unblocked.
     *
     * @param update The amount to increment the internal counter by. Defaults to 1.
     */
    void release(const std::ptrdiff_t update = 1)
    {
        {
            const std::scoped_lock lock(mutex);
            counter += update;
        }
        cv.notify_all();
    }

    /**
     * @brief Tries to atomically decrement the internal counter by 1 if it is greater than 0; no blocking occurs regardless.
     *
     * @return `true` if decremented the internal counter, `false` otherwise.
     */
    bool try_acquire()
    {
        std::scoped_lock lock(mutex);
        if (counter > 0)
        {
            --counter;
            return true;
        }
        return false;
    }

    /**
     * @brief Tries to atomically decrement the internal counter by 1 if it is greater than 0; otherwise blocks until it is greater than 0 and can successfully decrement the internal counter, or the `rel_time` duration has been exceeded.
     *
     * @tparam Rep An arithmetic type representing the number of ticks to wait.
     * @tparam Period An `std::ratio` representing the length of each tick in seconds.
     * @param rel_time The duration the function must wait. Note that the function may wait for longer.
     * @return `true` if decremented the internal counter, `false` otherwise.
     */
    template <class Rep, class Period>
    bool try_acquire_for(const std::chrono::duration<Rep, Period>& rel_time)
    {
        std::unique_lock lock(mutex);
        if (!cv.wait_for(lock, rel_time,
                [this]
                {
                    return counter > 0;
                }))
            return false;
        --counter;
        return true;
    }

    /**
     * @brief Tries to atomically decrement the internal counter by 1 if it is greater than 0; otherwise blocks until it is greater than 0 and can successfully decrement the internal counter, or the `abs_time` time point has been passed.
     *
     * @tparam Clock The type of the clock used to measure time.
     * @tparam Duration An `std::chrono::duration` type used to indicate the time point.
     * @param abs_time The earliest time the function must wait until. Note that the function may wait for longer.
     * @return `true` if decremented the internal counter, `false` otherwise.
     */
    template <class Clock, class Duration>
    bool try_acquire_until(const std::chrono::time_point<Clock, Duration>& abs_time)
    {
        std::unique_lock lock(mutex);
        if (!cv.wait_until(lock, abs_time,
                [this]
                {
                    return counter > 0;
                }))
            return false;
        --counter;
        return true;
    }

private:
    /**
     * @brief The semaphore's counter.
     */
    std::ptrdiff_t counter;

    /**
     * @brief A condition variable used to wait for the counter.
     */
    std::condition_variable cv;

    /**
     * @brief A mutex used to synchronize access to the counter.
     */
    mutable std::mutex mutex;
};

/**
 * @brief A polyfill for `std::binary_semaphore`, to be used if C++20 features are not available.
 */
using binary_semaphore = counting_semaphore<1>;
#endif
} // namespace BS
#endif // BS_THREAD_POOL_HPP
