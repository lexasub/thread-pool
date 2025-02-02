#ifndef MULTI_FUTURE_H
#define MULTI_FUTURE_H
#include <vector>
#include <future>
/**
 * @brief A helper class to facilitate waiting for and/or getting the results of multiple futures at once.
 *
 * @tparam T The return type of the futures.
 */
namespace BS {
    template <typename T>
    class [[nodiscard]] multi_future : public std::vector<std::future<T>>
    {
        public:
        // Inherit all constructors from the base class `std::vector`.
        using std::vector<std::future<T>>::vector;

        /**
         * @brief Get the results from all the futures stored in this `BS::multi_future`, rethrowing any stored exceptions.
         *
         * @return If the futures return `void`, this function returns `void` as well. Otherwise, it returns a vector containing the results.
         */
        [[nodiscard]] std::conditional_t<std::is_void_v<T>, void, std::vector<T>> get()
        {
            if constexpr (std::is_void_v<T>)
            {
                for (std::future<T>& future : *this)
                    future.get();
                return;
            }
            else
            {
                std::vector<T> results;
                results.reserve(this->size());
                for (std::future<T>& future : *this)
                    results.push_back(future.get());
                return results;
            }
        }

        /**
         * @brief Check how many of the futures stored in this `BS::multi_future` are ready.
         *
         * @return The number of ready futures.
         */
        [[nodiscard]] std::size_t ready_count() const
        {
            std::size_t count = 0;
            for (const std::future<T>& future : *this)
            {
                if (future.wait_for(std::chrono::duration<double>::zero()) == std::future_status::ready)
                    ++count;
            }
            return count;
        }

        /**
         * @brief Check if all the futures stored in this `BS::multi_future` are valid.
         *
         * @return `true` if all futures are valid, `false` if at least one of the futures is not valid.
         */
        [[nodiscard]] bool valid() const noexcept
        {
            bool is_valid = true;
            for (const std::future<T>& future : *this)
                is_valid = is_valid && future.valid();
            return is_valid;
        }

        /**
         * @brief Wait for all the futures stored in this `BS::multi_future`.
         */
        void wait() const
        {
            for (const std::future<T>& future : *this)
                future.wait();
        }

        /**
         * @brief Wait for all the futures stored in this `BS::multi_future`, but stop waiting after the specified duration has passed. This function first waits for the first future for the desired duration. If that future is ready before the duration expires, this function waits for the second future for whatever remains of the duration. It continues similarly until the duration expires.
         *
         * @tparam R An arithmetic type representing the number of ticks to wait.
         * @tparam P An `std::ratio` representing the length of each tick in seconds.
         * @param duration The amount of time to wait.
         * @return `true` if all futures have been waited for before the duration expired, `false` otherwise.
         */
        template <typename R, typename P>
        bool wait_for(const std::chrono::duration<R, P>& duration) const
        {
            const std::chrono::time_point<std::chrono::steady_clock> start_time = std::chrono::steady_clock::now();
            for (const std::future<T>& future : *this)
            {
                future.wait_for(duration - (std::chrono::steady_clock::now() - start_time));
                if (duration < std::chrono::steady_clock::now() - start_time)
                    return false;
            }
            return true;
        }

        /**
         * @brief Wait for all the futures stored in this `BS::multi_future`, but stop waiting after the specified time point has been reached. This function first waits for the first future until the desired time point. If that future is ready before the time point is reached, this function waits for the second future until the desired time point. It continues similarly until the time point is reached.
         *
         * @tparam C The type of the clock used to measure time.
         * @tparam D An `std::chrono::duration` type used to indicate the time point.
         * @param timeout_time The time point at which to stop waiting.
         * @return `true` if all futures have been waited for before the time point was reached, `false` otherwise.
         */
        template <typename C, typename D>
        bool wait_until(const std::chrono::time_point<C, D>& timeout_time) const
        {
            for (const std::future<T>& future : *this)
            {
                future.wait_until(timeout_time);
                if (timeout_time < std::chrono::steady_clock::now())
                    return false;
            }
            return true;
        }
    }; // class multi_future
}

#endif //MULTI_FUTURE_H
