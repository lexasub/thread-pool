#ifndef THREAD_POOL_PRE_H
#define THREAD_POOL_PRE_H
// In C++20 and later we can use concepts. In C++17 we instead use SFINAE ("Substitution Failure Is Not An Error") with `std::enable_if_t`.
#ifdef __cpp_concepts
    #define BS_THREAD_POOL_IF_PAUSE_ENABLED template <bool P = pause_enabled> requires(P)
template <typename F>
concept init_func_c = std::invocable<F> || std::invocable<F, std::size_t>;
#define BS_THREAD_POOL_INIT_FUNC_CONCEPT(F) init_func_c F
#else
#define BS_THREAD_POOL_IF_PAUSE_ENABLED template <bool P = pause_enabled, typename = std::enable_if_t<P>>
#define BS_THREAD_POOL_INIT_FUNC_CONCEPT(F) typename F, typename = std::enable_if_t<std::is_invocable_v<F> || std::is_invocable_v<F, std::size_t>> // NOLINT(bugprone-macro-parentheses)
#endif

/**
 * @brief A meta-programming template to determine the common type of two integer types. Unlike `std::common_type`, this template maintains correct signedness.
 *
 * @tparam T1 The first type.
 * @tparam T2 The second type.
 * @tparam Enable A dummy parameter to enable SFINAE in specializations.
 */
template <typename T1, typename T2, typename Enable = void>
struct common_index_type
{
    // Fallback to `std::common_type_t` if no specialization matches.
    using type = std::common_type_t<T1, T2>;
};

// The common type of two signed integers is the larger of the integers, with the same signedness.
template <typename T1, typename T2>
struct common_index_type<T1, T2, std::enable_if_t<std::is_signed_v<T1> && std::is_signed_v<T2>>>
{
    using type = std::conditional_t<(sizeof(T1) >= sizeof(T2)), T1, T2>;
};

// The common type of two unsigned integers is the larger of the integers, with the same signedness.
template <typename T1, typename T2>
struct common_index_type<T1, T2, std::enable_if_t<std::is_unsigned_v<T1> && std::is_unsigned_v<T2>>>
{
    using type = std::conditional_t<(sizeof(T1) >= sizeof(T2)), T1, T2>;
};

// The common type of a signed and an unsigned integer is a signed integer that can hold the full ranges of both integers.
template <typename T1, typename T2>
struct common_index_type<T1, T2, std::enable_if_t<(std::is_signed_v<T1> && std::is_unsigned_v<T2>) || (std::is_unsigned_v<T1> && std::is_signed_v<T2>)>>
{
    using S = std::conditional_t<std::is_signed_v<T1>, T1, T2>;
    using U = std::conditional_t<std::is_unsigned_v<T1>, T1, T2>;
    static constexpr std::size_t larger_size = (sizeof(S) > sizeof(U)) ? sizeof(S) : sizeof(U);
    using type = std::conditional_t<larger_size <= 4,
        // If both integers are 32 bits or less, the common type should be a signed type that can hold both of them. If both are 8 bits, or the signed type is 16 bits and the unsigned type is 8 bits, the common type is `std::int16_t`. Otherwise, if both are 16 bits, or the signed type is 32 bits and the unsigned type is smaller, the common type is `std::int32_t`. Otherwise, if both are 32 bits or less, the common type is `std::int64_t`.
        std::conditional_t<larger_size == 1 || (sizeof(S) == 2 && sizeof(U) == 1), std::int16_t, std::conditional_t<larger_size == 2 || (sizeof(S) == 4 && sizeof(U) < 4), std::int32_t, std::int64_t>>,
        // If the unsigned integer is 64 bits, the common type should also be an unsigned 64-bit integer, that is, `std::uint64_t`. The reason is that the most common scenario where this might happen is where the indices go from 0 to `x` where `x` has been previously defined as `std::size_t`, e.g. the size of a vector. Note that this will fail if the first index is negative; in that case, the user must cast the indices explicitly to the desired common type. If the unsigned integer is not 64 bits, then the signed integer must be 64 bits, hence the common type is `std::int64_t`.
        std::conditional_t<sizeof(U) == 8, std::uint64_t, std::int64_t>>;
};

using rand_priority_t = std::int16_t;

/**
 * @brief A fast, lightweight, modern, and easy-to-use C++17/C++20/C++23 thread pool class.
 *
 * @tparam OptFlags A bitmask of flags which can be used to enable optional features. The flags are members of the `BS::tp` enumeration: `BS::tp::priority`, `BS::tp::pause`, and `BS::tp::wait_deadlock_checks`. The default is `BS::tp::none`, which disables all optional features. To enable multiple features, use the bitwise OR operator `|`, e.g. `BS::tp::priority | BS::tp::pause`.
 */

#ifdef __cpp_lib_jthread
/**
 * @brief The type of threads to use. In C++20 and later we use `std::jthread`.
 */
using thread_t = std::jthread;
// The following macros are used to determine how to stop the workers. In C++20 and later we can use `std::stop_token`.
#define BS_THREAD_POOL_WORKER_TOKEN const std::stop_token &stop_token,
#define BS_THREAD_POOL_WAIT_TOKEN , stop_token
#define BS_THREAD_POOL_STOP_CONDITION stop_token.stop_requested()
#define BS_THREAD_POOL_OR_STOP_CONDITION
#else
/**
 * @brief The type of threads to use. In C++17 we use`std::thread`.
 */
using thread_t = std::thread;
// The following macros are used to determine how to stop the workers. In C++17 we use a manual flag `workers_running`.
#define BS_THREAD_POOL_WORKER_TOKEN
#define BS_THREAD_POOL_WAIT_TOKEN
#define BS_THREAD_POOL_STOP_CONDITION !workers_running
#define BS_THREAD_POOL_OR_STOP_CONDITION || !workers_running
#endif

namespace BS {
    /**
     * @brief A helper type alias to obtain the common type from the template `BS::common_index_type`.
     *
     * @tparam T1 The first type.
     * @tparam T2 The second type.
     */
    template <typename T1, typename T2>
    using common_index_type_t = typename common_index_type<T1, T2>::type;
    /**
     * @brief A type used to indicate the priority of a task. Defined to be a signed integer with a width of exactly 8 bits (-128 to +127).
     */
    using priority_t = std::int8_t;
    /**
     * @brief The type used for the bitmask template parameter of the thread pool.
     */
    #ifdef __cpp_lib_move_only_function
    /**
     * @brief The template to use to store functions in the task queue and other places. In C++23 and later we use `std::move_only_function`.
     */
    template <typename... S>
    using function_t = std::move_only_function<S...>;
    #else
    /**
     * @brief The template to use to store functions in the task queue and other places. In C++17 we use `std::function`.
     */
    template <typename... S>
    using function_t = std::function<S...>;
    #endif
    /**
     * @brief The type of tasks in the task queue.
     */
    using task_t = function_t<void()>;

    #ifdef __cpp_exceptions
    /**
     * @brief An exception that will be thrown by `wait()`, `wait_for()`, and `wait_until()` if the user tries to call them from within a thread of the same pool, which would result in a deadlock. Only used if the flag `BS:tp::wait_deadlock_checks` is enabled in the template parameter of `BS::thread_pool`.
     */
    struct wait_deadlock : public std::runtime_error
    {
        wait_deadlock() : std::runtime_error("BS::wait_deadlock") {};
    };
    #endif
}
#endif //THREAD_POOL_PRE_H
