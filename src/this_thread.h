#ifndef THIS_THREAD_H
#define THIS_THREAD_H

/**
 * @brief A class used to obtain information about the current thread and, if native extensions are enabled, set its priority and affinity.
 */
namespace BS {
    using opt_t = std::uint8_t;
    /**
     * @brief An enumeration of flags to be used in the bitmask template parameter of `BS::thread_pool` to enable optional features.
     */
    enum tp : opt_t
    {
        /**
         * @brief No optional features enabled.
         */
        none = 0,

        /**
         * @brief Enable task priority.
         */
        priority = 1 << 0,

        /**
         * @brief Enable pausing.
         */
        pause = 1 << 2,

        /**
         * @brief Enable wait deadlock checks.
         */
        wait_deadlock_checks = 1 << 3
    };
    template <opt_t OptFlags = tp::none>
    class [[nodiscard]] thread_pool;
    class [[nodiscard]] this_thread
    {
        template <opt_t>
        friend class thread_pool;

    public:
        /**
         * @brief Get the index of the current thread. If this thread belongs to a `BS::thread_pool` object, the return value will be an index in the range `[0, N)` where `N == BS::thread_pool::get_thread_count()`. Otherwise, for example if this thread is the main thread or an independent thread not in any pools, `std::nullopt` will be returned.
         *
         * @return An `std::optional` object, optionally containing a thread index.
         */
        [[nodiscard]] static std::optional<std::size_t> get_index() noexcept
        {
            return my_index;
        }

        /**
         * @brief Get a pointer to the thread pool that owns the current thread. If this thread belongs to a `BS::thread_pool` object, the return value will be a `void` pointer to that object. Otherwise, for example if this thread is the main thread or an independent thread not in any pools, `std::nullopt` will be returned.
         *
         * @return An `std::optional` object, optionally containing a pointer to a thread pool. Note that this will be a `void` pointer, so it must be cast to the desired instantiation of the `BS::thread_pool` template in order to use any member functions.
         */
        [[nodiscard]] static std::optional<void*> get_pool() noexcept
        {
            return my_pool;
        }

    #ifdef BS_THREAD_POOL_NATIVE_EXTENSIONS
        /**
         * @brief Get the processor affinity of the current thread using the current platform's native API. This should work on Windows and Linux, but is not possible on macOS as the native API does not allow it.
         *
         * @return An `std::optional` object, optionally containing the processor affinity of the current thread as an `std::vector<bool>` where each element corresponds to a logical processor. If the returned object does not contain a value, then the affinity could not be determined. On macOS, this function always returns `std::nullopt`.
         */
        [[nodiscard]] static std::optional<std::vector<bool>> get_os_thread_affinity()
        {
        #if defined(_WIN32)
            // Windows does not have a `GetThreadAffinityMask()` function, but `SetThreadAffinityMask()` returns the previous affinity mask, so we can use that to get the current affinity and then restore it. It's a bit of a hack, but it works. Since the thread affinity must be a subset of the process affinity, we use the process affinity as the temporary value.
            DWORD_PTR process_mask = 0;
            DWORD_PTR system_mask = 0;
            if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) == 0)
                return std::nullopt;
            const DWORD_PTR previous_mask = SetThreadAffinityMask(GetCurrentThread(), process_mask);
            if (previous_mask == 0)
                return std::nullopt;
            SetThreadAffinityMask(GetCurrentThread(), previous_mask);
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
                affinity[i] = ((previous_mask & (1ULL << i)) != 0ULL);
            return affinity;
        #elif defined(__linux__)
            cpu_set_t cpu_set;
            CPU_ZERO(&cpu_set);
            if (pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set) != 0)
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
         * @brief Set the processor affinity of the current thread using the current platform's native API. This should work on Windows and Linux, but is not possible on macOS as the native API does not allow it. Note that the thread affinity must be a subset of the process affinity (as obtained using `BS::get_os_process_affinity()`) for the containing process of a thread.
         *
         * @param affinity The processor affinity to set, as an `std::vector<bool>` where each element corresponds to a logical processor.
         * @return `true` if the affinity was set successfully, `false` otherwise. On macOS, this function always returns `false`.
         */
        static bool set_os_thread_affinity(const std::vector<bool>& affinity)
        {
        #if defined(_WIN32)
            DWORD_PTR thread_mask = 0;
            for (std::size_t i = 0; i < std::min<std::size_t>(affinity.size(), sizeof(DWORD_PTR) * 8); ++i)
                thread_mask |= (affinity[i] ? (1ULL << i) : 0ULL);
            return SetThreadAffinityMask(GetCurrentThread(), thread_mask) != 0;
        #elif defined(__linux__)
            cpu_set_t cpu_set;
            CPU_ZERO(&cpu_set);
            for (std::size_t i = 0; i < std::min<std::size_t>(affinity.size(), CPU_SETSIZE); ++i)
            {
                if (affinity[i])
                    CPU_SET(i, &cpu_set);
            }
            return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpu_set) == 0;
        #elif defined(__APPLE__)
            return affinity[0] && false; // NOLINT(readability-simplify-boolean-expr) // Using `affinity` to suppress unused parameter warning.
        #endif
        }

        /**
         * @brief Get the name of the current thread using the current platform's native API. This should work on Windows, Linux, and macOS.
         *
         * @return An `std::optional` object, optionally containing the name of the current thread. If the returned object does not contain a value, then the name could not be determined.
         */
        [[nodiscard]] static std::optional<std::string> get_os_thread_name()
        {
        #if defined(_WIN32)
            // On Windows thread names are wide strings, so we need to convert them to normal strings.
            PWSTR data = nullptr;
            const HRESULT hr = GetThreadDescription(GetCurrentThread(), &data);
            if (FAILED(hr))
                return std::nullopt;
            if (data == nullptr)
                return std::nullopt;
            const int size = WideCharToMultiByte(CP_UTF8, 0, data, -1, nullptr, 0, nullptr, nullptr);
            if (size == 0)
            {
                LocalFree(data);
                return std::nullopt;
            }
            std::string name(static_cast<std::size_t>(size) - 1, 0);
            const int result = WideCharToMultiByte(CP_UTF8, 0, data, -1, name.data(), size, nullptr, nullptr);
            LocalFree(data);
            if (result == 0)
                return std::nullopt;
            return name;
        #elif defined(__linux__) || defined(__APPLE__)
            #ifdef __linux__
            // On Linux thread names are limited to 16 characters, including the null terminator.
            constexpr std::size_t buffer_size = 16;
            #else
            // On macOS thread names are limited to 64 characters, including the null terminator.
            constexpr std::size_t buffer_size = 64;
            #endif
            char name[buffer_size] = {};
            if (pthread_getname_np(pthread_self(), name, buffer_size) != 0)
                return std::nullopt;
            return std::string(name);
        #endif
        }

        /**
         * @brief Set the name of the current thread using the current platform's native API. This should work on Windows, Linux, and macOS. Note that on Linux thread names are limited to 16 characters, including the null terminator.
         *
         * @param name The name to set.
         * @return `true` if the name was set successfully, `false` otherwise.
         */
        static bool set_os_thread_name(const std::string& name)
        {
        #if defined(_WIN32)
            // On Windows thread names are wide strings, so we need to convert them from normal strings.
            const int size = MultiByteToWideChar(CP_UTF8, 0, name.data(), -1, nullptr, 0);
            if (size == 0)
                return false;
            std::wstring wide(static_cast<std::size_t>(size), 0);
            if (MultiByteToWideChar(CP_UTF8, 0, name.data(), -1, wide.data(), size) == 0)
                return false;
            const HRESULT hr = SetThreadDescription(GetCurrentThread(), wide.data());
            return SUCCEEDED(hr);
        #elif defined(__linux__)
            // On Linux this is straightforward.
            return pthread_setname_np(pthread_self(), name.data()) == 0;
        #elif defined(__APPLE__)
            // On macOS, unlike Linux, a thread can only set a name for itself, so the signature is different.
            return pthread_setname_np(name.data()) == 0;
        #endif
        }

        /**
         * @brief Get the priority of the current thread using the current platform's native API. This should work on Windows, Linux, and macOS.
         *
         * @return An `std::optional` object, optionally containing the priority of the current thread, as a member of the enum `BS::os_thread_priority`. If the returned object does not contain a value, then either the priority could not be determined, or it is not one of the pre-defined values.
         */
        [[nodiscard]] static std::optional<os_thread_priority> get_os_thread_priority()
        {
        #if defined(_WIN32)
            // On Windows, this is straightforward.
            const int priority = GetThreadPriority(GetCurrentThread());
            if (priority == THREAD_PRIORITY_ERROR_RETURN)
                return std::nullopt;
            return static_cast<os_thread_priority>(priority);
        #elif defined(__linux__)
            // On Linux, we distill the choices of scheduling policy, priority, and "nice" value into 7 pre-defined levels, for simplicity and portability. The total number of possible combinations of policies and priorities is much larger, so if the value was set via any means other than `BS::this_thread::set_os_thread_priority()`, it may not match one of our pre-defined values.
            int policy = 0;
            struct sched_param param = {};
            if (pthread_getschedparam(pthread_self(), &policy, &param) != 0)
                return std::nullopt;
            if (policy == SCHED_FIFO && param.sched_priority == sched_get_priority_max(SCHED_FIFO))
            {
                // The only pre-defined priority that uses SCHED_FIFO and the maximum available priority value is the "realtime" priority.
                return os_thread_priority::realtime;
            }
            if (policy == SCHED_RR && param.sched_priority == sched_get_priority_min(SCHED_RR) + (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR)) / 2)
            {
                // The only pre-defined priority that uses SCHED_RR and a priority in the middle of the available range is the "highest" priority.
                return os_thread_priority::highest;
            }
            #ifdef __linux__
            if (policy == SCHED_IDLE)
            {
                // The only pre-defined priority that uses SCHED_IDLE is the "idle" priority. Note that this scheduling policy is not available on macOS.
                return os_thread_priority::idle;
            }
            #endif
            if (policy == SCHED_OTHER)
            {
                // For SCHED_OTHER, the result depends on the "nice" value. The usual range is -20 to 19 or 20, with higher values corresponding to lower priorities. Note that `getpriority()` returns -1 on error, but since this does not correspond to any of our pre-defined values, this function will return `std::nullopt` anyway.
                const int nice_val = getpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)));
                switch (nice_val)
                {
                case PRIO_MIN + 2:
                    return os_thread_priority::above_normal;
                case 0:
                    return os_thread_priority::normal;
                case (PRIO_MAX / 2) + (PRIO_MAX % 2):
                    return os_thread_priority::below_normal;
                case PRIO_MAX - 3:
                    return os_thread_priority::lowest;
            #ifdef __APPLE__
                // `SCHED_IDLE` doesn't exist on macOS, so we use the policy `SCHED_OTHER` with a "nice" value of `PRIO_MAX - 2`.
                case PRIO_MAX - 2:
                    return os_thread_priority::idle;
            #endif
                default:
                    return std::nullopt;
                }
            }
            return std::nullopt;
        #elif defined(__APPLE__)
            // On macOS, we distill the choices of scheduling policy and priority into 7 pre-defined levels, for simplicity and portability. The total number of possible combinations of policies and priorities is much larger, so if the value was set via any means other than `BS::this_thread::set_os_thread_priority()`, it may not match one of our pre-defined values.
            int policy = 0;
            struct sched_param param = {};
            if (pthread_getschedparam(pthread_self(), &policy, &param) != 0)
                return std::nullopt;
            if (policy == SCHED_FIFO && param.sched_priority == sched_get_priority_max(SCHED_FIFO))
            {
                // The only pre-defined priority that uses SCHED_FIFO and the maximum available priority value is the "realtime" priority.
                return os_thread_priority::realtime;
            }
            if (policy == SCHED_RR && param.sched_priority == sched_get_priority_min(SCHED_RR) + (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR)) / 2)
            {
                // The only pre-defined priority that uses SCHED_RR and a priority in the middle of the available range is the "highest" priority.
                return os_thread_priority::highest;
            }
            if (policy == SCHED_OTHER)
            {
                // For SCHED_OTHER, the result depends on the specific value of the priority.
                if (param.sched_priority == sched_get_priority_max(SCHED_OTHER))
                    return os_thread_priority::above_normal;
                if (param.sched_priority == sched_get_priority_min(SCHED_OTHER) + (sched_get_priority_max(SCHED_OTHER) - sched_get_priority_min(SCHED_OTHER)) / 2)
                    return os_thread_priority::normal;
                if (param.sched_priority == sched_get_priority_min(SCHED_OTHER) + (sched_get_priority_max(SCHED_OTHER) - sched_get_priority_min(SCHED_OTHER)) * 2 / 3)
                    return os_thread_priority::below_normal;
                if (param.sched_priority == sched_get_priority_min(SCHED_OTHER) + (sched_get_priority_max(SCHED_OTHER) - sched_get_priority_min(SCHED_OTHER)) / 3)
                    return os_thread_priority::lowest;
                if (param.sched_priority == sched_get_priority_min(SCHED_OTHER))
                    return os_thread_priority::idle;
                return std::nullopt;
            }
            return std::nullopt;
        #endif
        }

        /**
         * @brief Set the priority of the current thread using the current platform's native API. This should work on Windows, Linux, and macOS. However, note that higher priorities might require elevated permissions.
         *
         * @param priority The priority to set. Must be a value from the enum `BS::os_thread_priority`.
         * @return `true` if the priority was set successfully, `false` otherwise. Usually, `false` means that the user does not have the necessary permissions to set the desired priority.
         */
        static bool set_os_thread_priority(const os_thread_priority priority)
        {
        #if defined(_WIN32)
            // On Windows, this is straightforward.
            return SetThreadPriority(GetCurrentThread(), static_cast<int>(priority)) != 0;
        #elif defined(__linux__)
            // On Linux, we distill the choices of scheduling policy, priority, and "nice" value into 7 pre-defined levels, for simplicity and portability. The total number of possible combinations of policies and priorities is much larger, but allowing more fine-grained control would not be portable.
            int policy = 0;
            struct sched_param param = {};
            std::optional<int> nice_val = std::nullopt;
            switch (priority)
            {
            case os_thread_priority::realtime:
                // "Realtime" pre-defined priority: We use the policy `SCHED_FIFO` with the highest possible priority.
                policy = SCHED_FIFO;
                param.sched_priority = sched_get_priority_max(SCHED_FIFO);
                break;
            case os_thread_priority::highest:
                // "Highest" pre-defined priority: We use the policy `SCHED_RR` ("round-robin") with a priority in the middle of the available range.
                policy = SCHED_RR;
                param.sched_priority = sched_get_priority_min(SCHED_RR) + (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR)) / 2;
                break;
            case os_thread_priority::above_normal:
                // "Above normal" pre-defined priority: We use the policy `SCHED_OTHER` (the default). This policy does not accept a priority value, so priority must be 0. However, we set the "nice" value to the minimum value as given by `PRIO_MIN`, plus 2 (which should evaluate to -18). The usual range is -20 to 19 or 20, with higher values corresponding to lower priorities.
                policy = SCHED_OTHER;
                param.sched_priority = 0;
                nice_val = PRIO_MIN + 2;
                break;
            case os_thread_priority::normal:
                // "Normal" pre-defined priority: We use the policy `SCHED_OTHER`, priority must be 0, and we set the "nice" value to 0 (the default).
                policy = SCHED_OTHER;
                param.sched_priority = 0;
                nice_val = 0;
                break;
            case os_thread_priority::below_normal:
                // "Below normal" pre-defined priority: We use the policy `SCHED_OTHER`, priority must be 0, and we set the "nice" value to half the maximum value as given by `PRIO_MAX`, rounded up (which should evaluate to 10).
                policy = SCHED_OTHER;
                param.sched_priority = 0;
                nice_val = (PRIO_MAX / 2) + (PRIO_MAX % 2);
                break;
            case os_thread_priority::lowest:
                // "Lowest" pre-defined priority: We use the policy `SCHED_OTHER`, priority must be 0, and we set the "nice" value to the maximum value as given by `PRIO_MAX`, minus 3 (which should evaluate to 17).
                policy = SCHED_OTHER;
                param.sched_priority = 0;
                nice_val = PRIO_MAX - 3;
                break;
            case os_thread_priority::idle:
                // "Idle" pre-defined priority on Linux: We use the policy `SCHED_IDLE`, priority must be 0, and we don't touch the "nice" value.
                policy = SCHED_IDLE;
                param.sched_priority = 0;
                break;
            default:
                return false;
            }
            bool success = (pthread_setschedparam(pthread_self(), policy, &param) == 0);
            if (nice_val.has_value())
                success = success && (setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), nice_val.value()) == 0);
            return success;
        #elif defined(__APPLE__)
            // On macOS, unlike Linux, the "nice" value is per-process, not per-thread (in compliance with the POSIX standard). However, unlike Linux, `SCHED_OTHER` on macOS does have a range of priorities. So for `realtime` and `highest` priorities we use `SCHED_FIFO` and `SCHED_RR` respectively as for Linux, but for the other priorities we use `SCHED_OTHER` with a priority in the range given by `sched_get_priority_min(SCHED_OTHER)` to `sched_get_priority_max(SCHED_OTHER)`.
            int policy = 0;
            struct sched_param param = {};
            switch (priority)
            {
            case os_thread_priority::realtime:
                // "Realtime" pre-defined priority: We use the policy `SCHED_FIFO` with the highest possible priority.
                policy = SCHED_FIFO;
                param.sched_priority = sched_get_priority_max(SCHED_FIFO);
                break;
            case os_thread_priority::highest:
                // "Highest" pre-defined priority: We use the policy `SCHED_RR` ("round-robin") with a priority in the middle of the available range.
                policy = SCHED_RR;
                param.sched_priority = sched_get_priority_min(SCHED_RR) + (sched_get_priority_max(SCHED_RR) - sched_get_priority_min(SCHED_RR)) / 2;
                break;
            case os_thread_priority::above_normal:
                // "Above normal" pre-defined priority: We use the policy `SCHED_OTHER` (the default) with the highest possible priority.
                policy = SCHED_OTHER;
                param.sched_priority = sched_get_priority_max(SCHED_OTHER);
                break;
            case os_thread_priority::normal:
                // "Normal" pre-defined priority: We use the policy `SCHED_OTHER` (the default) with a priority in the middle of the available range (which appears to be the default?).
                policy = SCHED_OTHER;
                param.sched_priority = sched_get_priority_min(SCHED_OTHER) + (sched_get_priority_max(SCHED_OTHER) - sched_get_priority_min(SCHED_OTHER)) / 2;
                break;
            case os_thread_priority::below_normal:
                // "Below normal" pre-defined priority: We use the policy `SCHED_OTHER` (the default) with a priority equal to 2/3rds of the normal value.
                policy = SCHED_OTHER;
                param.sched_priority = sched_get_priority_min(SCHED_OTHER) + (sched_get_priority_max(SCHED_OTHER) - sched_get_priority_min(SCHED_OTHER)) * 2 / 3;
                break;
            case os_thread_priority::lowest:
                // "Lowest" pre-defined priority: We use the policy `SCHED_OTHER` (the default) with a priority equal to 1/3rd of the normal value.
                policy = SCHED_OTHER;
                param.sched_priority = sched_get_priority_min(SCHED_OTHER) + (sched_get_priority_max(SCHED_OTHER) - sched_get_priority_min(SCHED_OTHER)) / 3;
                break;
            case os_thread_priority::idle:
                // "Idle" pre-defined priority on macOS: We use the policy `SCHED_OTHER` (the default) with the lowest possible priority.
                policy = SCHED_OTHER;
                param.sched_priority = sched_get_priority_min(SCHED_OTHER);
                break;
            default:
                return false;
            }
            return pthread_setschedparam(pthread_self(), policy, &param) == 0;
        #endif
        }
    #endif

    private:
        inline static thread_local std::optional<std::size_t> my_index = std::nullopt;
        inline static thread_local std::optional<void*> my_pool = std::nullopt;
    }; // class this_thread
}
#endif //THIS_THREAD_H
