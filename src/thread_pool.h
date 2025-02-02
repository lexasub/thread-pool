#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include "blocks.h"
#include "multi_future.h"
#include "this_thread.h"
#include "thread_pool_pre.h"
namespace BS {
    template <opt_t OptFlags>
    class [[nodiscard]] thread_pool
    {
    public:
        /**
         * @brief A flag indicating whether task priority is enabled.
         */
        static constexpr bool priority_enabled = (OptFlags & tp::priority) != 0;

        /**
         * @brief A flag indicating whether pausing is enabled.
         */
        static constexpr bool pause_enabled = (OptFlags & tp::pause) != 0;

        /**
         * @brief A flag indicating whether wait deadlock checks are enabled.
         */
        static constexpr bool wait_deadlock_checks_enabled = (OptFlags & tp::wait_deadlock_checks) != 0;

    #ifndef __cpp_exceptions
        static_assert(!wait_deadlock_checks_enabled, "Wait deadlock checks cannot be enabled if exception handling is disabled.");
    #endif

        // ============================
        // Constructors and destructors
        // ============================

        /**
         * @brief Construct a new thread pool. The number of threads will be the total number of hardware threads available, as reported by the implementation. This is usually determined by the number of cores in the CPU. If a core is hyperthreaded, it will count as two threads.
         */
        thread_pool() : thread_pool(0, [] {}) {}

        /**
         * @brief Construct a new thread pool with the specified number of threads.
         *
         * @param num_threads The number of threads to use.
         */
        explicit thread_pool(const std::size_t num_threads) : thread_pool(num_threads, [] {}) {}

        /**
         * @brief Construct a new thread pool with the specified initialization function.
         *
         * @param init An initialization function to run in each thread before it starts executing any submitted tasks. The function must have no return value, and can either take one argument, the thread index of type `std::size_t`, or zero arguments. It will be executed exactly once per thread, when the thread is first constructed. The initialization function must not throw any exceptions, as that will result in program termination. Any exceptions must be handled explicitly within the function.
         */
        template <BS_THREAD_POOL_INIT_FUNC_CONCEPT(F)>
        explicit thread_pool(F&& init) : thread_pool(0, std::forward<F>(init))
        {
        }

        /**
         * @brief Construct a new thread pool with the specified number of threads and initialization function.
         *
         * @param num_threads The number of threads to use.
         * @param init An initialization function to run in each thread before it starts executing any submitted tasks. The function must have no return value, and can either take one argument, the thread index of type `std::size_t`, or zero arguments. It will be executed exactly once per thread, when the thread is first constructed. The initialization function must not throw any exceptions, as that will result in program termination. Any exceptions must be handled explicitly within the function.
         */
        template <BS_THREAD_POOL_INIT_FUNC_CONCEPT(F)>
        thread_pool(const std::size_t num_threads, F&& init)
        {
            create_threads(num_threads, std::forward<F>(init));
        }

        // The copy and move constructors and assignment operators are deleted. The thread pool cannot be copied or moved.
        thread_pool(const thread_pool&) = delete;
        thread_pool(thread_pool&&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;
        thread_pool& operator=(thread_pool&&) = delete;

        /**
         * @brief Destruct the thread pool. Waits for all tasks to complete, then destroys all threads. If a cleanup function was set, it will run in each thread right before it is destroyed. Note that if the pool is paused, then any tasks still in the queue will never be executed.
         */
        ~thread_pool() noexcept
        {
    #ifdef __cpp_exceptions
            try
            {
    #endif
                wait();
    #ifndef __cpp_lib_jthread
                destroy_threads();
    #endif
    #ifdef __cpp_exceptions
            }
            catch (...)
            {
            }
    #endif
        }

        // =======================
        // Public member functions
        // =======================

        /**
         * @brief Parallelize a loop by automatically splitting it into blocks and submitting each block separately to the queue, with the specified priority. The block function takes two arguments, the start and end of the block, so that it is only called once per block, but it is up to the user make sure the block function correctly deals with all the indices in each block. Does not return a `BS::multi_future`, so the user must use `wait()` or some other method to ensure that the loop finishes executing, otherwise bad things will happen.
         *
         * @tparam T1 The type of the first index. Should be a signed or unsigned integer.
         * @tparam T2 The type of the index after the last index. Should be a signed or unsigned integer.
         * @tparam F The type of the function to loop through.
         * @param first_index The first index in the loop.
         * @param index_after_last The index after the last index in the loop. The loop will iterate from `first_index` to `(index_after_last - 1)` inclusive. In other words, it will be equivalent to `for (T i = first_index; i < index_after_last; ++i)`. Note that if `index_after_last <= first_index`, no blocks will be submitted.
         * @param block A function that will be called once per block. Should take exactly two arguments: the first index in the block and the index after the last index in the block. `block(start, end)` should typically involve a loop of the form `for (T i = start; i < end; ++i)`.
         * @param num_blocks The maximum number of blocks to split the loop into. The default is 0, which means the number of blocks will be equal to the number of threads in the pool.
         * @param priority The priority of the tasks. Should be between -128 and +127 (a signed 8-bit integer). The default is 0. Only taken into account if the flag `BS:tp::priority` is enabled in the template parameter, otherwise has no effect.
         */
        template <typename T1, typename T2, typename T = common_index_type_t<T1, T2>, typename F>
        void detach_blocks(const T1 first_index, const T2 index_after_last, F&& block, const std::size_t num_blocks = 0, const priority_t priority = 0)
        {
            if (static_cast<T>(index_after_last) > static_cast<T>(first_index))
            {
                const std::shared_ptr<std::decay_t<F>> block_ptr = std::make_shared<std::decay_t<F>>(std::forward<F>(block));
                const blocks blks(static_cast<T>(first_index), static_cast<T>(index_after_last), num_blocks ? num_blocks : thread_count);
                for (std::size_t blk = 0; blk < blks.get_num_blocks(); ++blk)
                {
                    detach_task(
                        [block_ptr, start = blks.start(blk), end = blks.end(blk)]
                        {
                            (*block_ptr)(start, end);
                        },
                        priority);
                }
            }
        }

        /**
         * @brief Parallelize a loop by automatically splitting it into blocks and submitting each block separately to the queue, with the specified priority. The loop function takes one argument, the loop index, so that it is called many times per block. Does not return a `BS::multi_future`, so the user must use `wait()` or some other method to ensure that the loop finishes executing, otherwise bad things will happen.
         *
         * @tparam T1 The type of the first index. Should be a signed or unsigned integer.
         * @tparam T2 The type of the index after the last index. Should be a signed or unsigned integer.
         * @tparam F The type of the function to loop through.
         * @param first_index The first index in the loop.
         * @param index_after_last The index after the last index in the loop. The loop will iterate from `first_index` to `(index_after_last - 1)` inclusive. In other words, it will be equivalent to `for (T i = first_index; i < index_after_last; ++i)`. Note that if `index_after_last <= first_index`, no blocks will be submitted.
         * @param loop The function to loop through. Will be called once per index, many times per block. Should take exactly one argument: the loop index.
         * @param num_blocks The maximum number of blocks to split the loop into. The default is 0, which means the number of blocks will be equal to the number of threads in the pool.
         * @param priority The priority of the tasks. Should be between -128 and +127 (a signed 8-bit integer). The default is 0. Only taken into account if the flag `BS:tp::priority` is enabled in the template parameter, otherwise has no effect.
         */
        template <typename T1, typename T2, typename T = common_index_type_t<T1, T2>, typename F>
        void detach_loop(const T1 first_index, const T2 index_after_last, F&& loop, const std::size_t num_blocks = 0, const priority_t priority = 0)
        {
            if (static_cast<T>(index_after_last) > static_cast<T>(first_index))
            {
                const std::shared_ptr<std::decay_t<F>> loop_ptr = std::make_shared<std::decay_t<F>>(std::forward<F>(loop));
                const blocks blks(static_cast<T>(first_index), static_cast<T>(index_after_last), num_blocks ? num_blocks : thread_count);
                for (std::size_t blk = 0; blk < blks.get_num_blocks(); ++blk)
                {
                    detach_task(
                        [loop_ptr, start = blks.start(blk), end = blks.end(blk)]
                        {
                            for (T i = start; i < end; ++i)
                                (*loop_ptr)(i);
                        },
                        priority);
                }
            }
        }

        /**
         * @brief Submit a sequence of tasks enumerated by indices to the queue, with the specified priority. The sequence function takes one argument, the task index, and will be called once per index. Does not return a `BS::multi_future`, so the user must use `wait()` or some other method to ensure that the sequence finishes executing, otherwise bad things will happen.
         *
         * @tparam T1 The type of the first index. Should be a signed or unsigned integer.
         * @tparam T2 The type of the index after the last index. Should be a signed or unsigned integer.
         * @tparam F The type of the function used to define the sequence.
         * @param first_index The first index in the sequence.
         * @param index_after_last The index after the last index in the sequence. The sequence will iterate from `first_index` to `(index_after_last - 1)` inclusive. In other words, it will be equivalent to `for (T i = first_index; i < index_after_last; ++i)`. Note that if `index_after_last <= first_index`, no tasks will be submitted.
         * @param sequence The function used to define the sequence. Will be called once per index. Should take exactly one argument, the index.
         * @param priority The priority of the tasks. Should be between -128 and +127 (a signed 8-bit integer). The default is 0. Only taken into account if the flag `BS:tp::priority` is enabled in the template parameter, otherwise has no effect.
         */
        template <typename T1, typename T2, typename T = common_index_type_t<T1, T2>, typename F>
        void detach_sequence(const T1 first_index, const T2 index_after_last, F&& sequence, const priority_t priority = 0)
        {
            if (static_cast<T>(index_after_last) > static_cast<T>(first_index))
            {
                const std::shared_ptr<std::decay_t<F>> sequence_ptr = std::make_shared<std::decay_t<F>>(std::forward<F>(sequence));
                for (T i = static_cast<T>(first_index); i < static_cast<T>(index_after_last); ++i)
                {
                    detach_task(
                        [sequence_ptr, i]
                        {
                            (*sequence_ptr)(i);
                        },
                        priority);
                }
            }
        }

        /**
         * @brief Submit a function with no arguments and no return value into the task queue, with the specified priority. To submit a function with arguments, enclose it in a lambda expression. Does not return a future, so the user must use `wait()` or some other method to ensure that the task finishes executing, otherwise bad things will happen.
         *
         * @tparam F The type of the function.
         * @param task The function to submit.
         * @param priority The priority of the task. Should be between -128 and +127 (a signed 8-bit integer). The default is 0. Only taken into account if the flag `BS:tp::priority` is enabled in the template parameter, otherwise has no effect.
         */
        template <typename F>
        void detach_task(F&& task, const priority_t priority = 0)
        {
            {
                const std::scoped_lock tasks_lock(tasks_mutex);
                if constexpr (priority_enabled)
                    tasks.emplace(std::forward<F>(task), priority);
                else
                    tasks.emplace(std::forward<F>(task));
            }
            task_available_cv.notify_one();
        }

    #ifdef BS_THREAD_POOL_NATIVE_EXTENSIONS
        /**
         * @brief Get a vector containing the underlying implementation-defined thread handles for each of the pool's threads, as obtained by `std::thread::native_handle()` (or `std::jthread::native_handle()` in C++20 and later).
         *
         * @return The native thread handles.
         */
        [[nodiscard]] std::vector<thread_t::native_handle_type> get_native_handles() const
        {
            std::vector<thread_t::native_handle_type> native_handles(thread_count);
            for (std::size_t i = 0; i < thread_count; ++i)
                native_handles[i] = threads[i].native_handle();
            return native_handles;
        }
    #endif

        /**
         * @brief Get the number of tasks currently waiting in the queue to be executed by the threads.
         *
         * @return The number of queued tasks.
         */
        [[nodiscard]] std::size_t get_tasks_queued() const
        {
            const std::scoped_lock tasks_lock(tasks_mutex);
            return tasks.size();
        }

        /**
         * @brief Get the number of tasks currently being executed by the threads.
         *
         * @return The number of running tasks.
         */
        [[nodiscard]] std::size_t get_tasks_running() const
        {
            const std::scoped_lock tasks_lock(tasks_mutex);
            return tasks_running;
        }

        /**
         * @brief Get the total number of unfinished tasks: either still waiting in the queue, or running in a thread. Note that `get_tasks_total() == get_tasks_queued() + get_tasks_running()`.
         *
         * @return The total number of tasks.
         */
        [[nodiscard]] std::size_t get_tasks_total() const
        {
            const std::scoped_lock tasks_lock(tasks_mutex);
            return tasks_running + tasks.size();
        }

        /**
         * @brief Get the number of threads in the pool.
         *
         * @return The number of threads.
         */
        [[nodiscard]] std::size_t get_thread_count() const noexcept
        {
            return thread_count;
        }

        /**
         * @brief Get a vector containing the unique identifiers for each of the pool's threads, as obtained by `std::thread::get_id()` (or `std::jthread::get_id()` in C++20 and later).
         *
         * @return The unique thread identifiers.
         */
        [[nodiscard]] std::vector<thread_t::id> get_thread_ids() const
        {
            std::vector<thread_t::id> thread_ids(thread_count);
            for (std::size_t i = 0; i < thread_count; ++i)
                thread_ids[i] = threads[i].get_id();
            return thread_ids;
        }

        /**
         * @brief Check whether the pool is currently paused. Only enabled if the flag `BS:tp::pause` is enabled in the template parameter.
         *
         * @return `true` if the pool is paused, `false` if it is not paused.
         */
        BS_THREAD_POOL_IF_PAUSE_ENABLED
        [[nodiscard]] bool is_paused() const
        {
            const std::scoped_lock tasks_lock(tasks_mutex);
            return paused;
        }

        /**
         * @brief Pause the pool. The workers will temporarily stop retrieving new tasks out of the queue, although any tasks already executed will keep running until they are finished. Only enabled if the flag `BS:tp::pause` is enabled in the template parameter.
         */
        BS_THREAD_POOL_IF_PAUSE_ENABLED
        void pause()
        {
            const std::scoped_lock tasks_lock(tasks_mutex);
            paused = true;
        }

        /**
         * @brief Purge all the tasks waiting in the queue. Tasks that are currently running will not be affected, but any tasks still waiting in the queue will be discarded, and will never be executed by the threads. Please note that there is no way to restore the purged tasks.
         */
        void purge()
        {
            const std::scoped_lock tasks_lock(tasks_mutex);
            tasks = {};
        }

        /**
         * @brief Reset the pool with the total number of hardware threads available, as reported by the implementation. Waits for all currently running tasks to be completed, then destroys all threads in the pool and creates a new thread pool with the new number of threads. Any tasks that were waiting in the queue before the pool was reset will then be executed by the new threads. If the pool was paused before resetting it, the new pool will be paused as well.
         */
        void reset()
        {
            reset(0, [](std::size_t) {});
        }

        /**
         * @brief Reset the pool with a new number of threads. Waits for all currently running tasks to be completed, then destroys all threads in the pool and creates a new thread pool with the new number of threads. Any tasks that were waiting in the queue before the pool was reset will then be executed by the new threads. If the pool was paused before resetting it, the new pool will be paused as well.
         *
         * @param num_threads The number of threads to use.
         */
        void reset(const std::size_t num_threads)
        {
            reset(num_threads, [](std::size_t) {});
        }

        /**
         * @brief Reset the pool with the total number of hardware threads available, as reported by the implementation, and a new initialization function. Waits for all currently running tasks to be completed, then destroys all threads in the pool and creates a new thread pool with the new number of threads and initialization function. Any tasks that were waiting in the queue before the pool was reset will then be executed by the new threads. If the pool was paused before resetting it, the new pool will be paused as well.
         *
         * @param init An initialization function to run in each thread before it starts executing any submitted tasks. The function must have no return value, and can either take one argument, the thread index of type `std::size_t`, or zero arguments. It will be executed exactly once per thread, when the thread is first constructed. The initialization function must not throw any exceptions, as that will result in program termination. Any exceptions must be handled explicitly within the function.
         */
        template <BS_THREAD_POOL_INIT_FUNC_CONCEPT(F)>
        void reset(F&& init)
        {
            reset(0, std::forward<F>(init));
        }

        /**
         * @brief Reset the pool with a new number of threads and a new initialization function. Waits for all currently running tasks to be completed, then destroys all threads in the pool and creates a new thread pool with the new number of threads and initialization function. Any tasks that were waiting in the queue before the pool was reset will then be executed by the new threads. If the pool was paused before resetting it, the new pool will be paused as well.
         *
         * @param num_threads The number of threads to use.
         * @param init An initialization function to run in each thread before it starts executing any submitted tasks. The function must have no return value, and can either take one argument, the thread index of type `std::size_t`, or zero arguments. It will be executed exactly once per thread, when the thread is first constructed. The initialization function must not throw any exceptions, as that will result in program termination. Any exceptions must be handled explicitly within the function.
         */
        template <BS_THREAD_POOL_INIT_FUNC_CONCEPT(F)>
        void reset(const std::size_t num_threads, F&& init)
        {
            if constexpr (pause_enabled)
            {
                std::unique_lock tasks_lock(tasks_mutex);
                const bool was_paused = paused;
                paused = true;
                tasks_lock.unlock();
                reset_pool(num_threads, std::forward<F>(init));
                tasks_lock.lock();
                paused = was_paused;
            }
            else
            {
                reset_pool(num_threads, std::forward<F>(init));
            }
        }

        /**
         * @brief Set the thread pool's cleanup function.
         *
         * @param cleanup A cleanup function to run in each thread right before it is destroyed, which will happen when the pool is destructed or reset. The function must have no return value, and can either take one argument, the thread index of type `std::size_t`, or zero arguments. The cleanup function must not throw any exceptions, as that will result in program termination. Any exceptions must be handled explicitly within the function.
         */
        template <BS_THREAD_POOL_INIT_FUNC_CONCEPT(F)>
        void set_cleanup_func(F&& cleanup)
        {
            if constexpr (std::is_invocable_v<F, std::size_t>)
            {
                cleanup_func = std::forward<F>(cleanup);
            }
            else
            {
                cleanup_func = [cleanup = std::forward<F>(cleanup)](std::size_t)
                {
                    cleanup();
                };
            }
        }

        /**
         * @brief Parallelize a loop by automatically splitting it into blocks and submitting each block separately to the queue, with the specified priority. The block function takes two arguments, the start and end of the block, so that it is only called once per block, but it is up to the user make sure the block function correctly deals with all the indices in each block. Returns a `BS::multi_future` that contains the futures for all of the blocks.
         *
         * @tparam T1 The type of the first index. Should be a signed or unsigned integer.
         * @tparam T2 The type of the index after the last index. Should be a signed or unsigned integer.
         * @tparam F The type of the function to loop through.
         * @tparam R The return type of the function to loop through (can be `void`).
         * @param first_index The first index in the loop.
         * @param index_after_last The index after the last index in the loop. The loop will iterate from `first_index` to `(index_after_last - 1)` inclusive. In other words, it will be equivalent to `for (T i = first_index; i < index_after_last; ++i)`. Note that if `index_after_last <= first_index`, no blocks will be submitted, and an empty `BS::multi_future` will be returned.
         * @param block A function that will be called once per block. Should take exactly two arguments: the first index in the block and the index after the last index in the block. `block(start, end)` should typically involve a loop of the form `for (T i = start; i < end; ++i)`.
         * @param num_blocks The maximum number of blocks to split the loop into. The default is 0, which means the number of blocks will be equal to the number of threads in the pool.
         * @param priority The priority of the tasks. Should be between -128 and +127 (a signed 8-bit integer). The default is 0. Only taken into account if the flag `BS:tp::priority` is enabled in the template parameter, otherwise has no effect.
         * @return A `BS::multi_future` that can be used to wait for all the blocks to finish. If the block function returns a value, the `BS::multi_future` can also be used to obtain the values returned by each block.
         */
        template <typename T1, typename T2, typename T = common_index_type_t<T1, T2>, typename F, typename R = std::invoke_result_t<std::decay_t<F>, T, T>>
        [[nodiscard]] multi_future<R> submit_blocks(const T1 first_index, const T2 index_after_last, F&& block, const std::size_t num_blocks = 0, const priority_t priority = 0)
        {
            if (static_cast<T>(index_after_last) > static_cast<T>(first_index))
            {
                const std::shared_ptr<std::decay_t<F>> block_ptr = std::make_shared<std::decay_t<F>>(std::forward<F>(block));
                const blocks blks(static_cast<T>(first_index), static_cast<T>(index_after_last), num_blocks ? num_blocks : thread_count);
                multi_future<R> future;
                future.reserve(blks.get_num_blocks());
                for (std::size_t blk = 0; blk < blks.get_num_blocks(); ++blk)
                {
                    future.push_back(submit_task(
                        [block_ptr, start = blks.start(blk), end = blks.end(blk)]
                        {
                            return (*block_ptr)(start, end);
                        },
                        priority));
                }
                return future;
            }
            return {};
        }

        /**
         * @brief Parallelize a loop by automatically splitting it into blocks and submitting each block separately to the queue, with the specified priority. The loop function takes one argument, the loop index, so that it is called many times per block. It must have no return value. Returns a `BS::multi_future` that contains the futures for all of the blocks.
         *
         * @tparam T1 The type of the first index. Should be a signed or unsigned integer.
         * @tparam T2 The type of the index after the last index. Should be a signed or unsigned integer.
         * @tparam F The type of the function to loop through.
         * @param first_index The first index in the loop.
         * @param index_after_last The index after the last index in the loop. The loop will iterate from `first_index` to `(index_after_last - 1)` inclusive. In other words, it will be equivalent to `for (T i = first_index; i < index_after_last; ++i)`. Note that if `index_after_last <= first_index`, no tasks will be submitted, and an empty `BS::multi_future` will be returned.
         * @param loop The function to loop through. Will be called once per index, many times per block. Should take exactly one argument: the loop index. It cannot have a return value.
         * @param num_blocks The maximum number of blocks to split the loop into. The default is 0, which means the number of blocks will be equal to the number of threads in the pool.
         * @param priority The priority of the tasks. Should be between -128 and +127 (a signed 8-bit integer). The default is 0. Only taken into account if the flag `BS:tp::priority` is enabled in the template parameter, otherwise has no effect.
         * @return A `BS::multi_future` that can be used to wait for all the blocks to finish.
         */
        template <typename T1, typename T2, typename T = common_index_type_t<T1, T2>, typename F>
        [[nodiscard]] multi_future<void> submit_loop(const T1 first_index, const T2 index_after_last, F&& loop, const std::size_t num_blocks = 0, const priority_t priority = 0)
        {
            if (static_cast<T>(index_after_last) > static_cast<T>(first_index))
            {
                const std::shared_ptr<std::decay_t<F>> loop_ptr = std::make_shared<std::decay_t<F>>(std::forward<F>(loop));
                const blocks blks(static_cast<T>(first_index), static_cast<T>(index_after_last), num_blocks ? num_blocks : thread_count);
                multi_future<void> future;
                future.reserve(blks.get_num_blocks());
                for (std::size_t blk = 0; blk < blks.get_num_blocks(); ++blk)
                {
                    future.push_back(submit_task(
                        [loop_ptr, start = blks.start(blk), end = blks.end(blk)]
                        {
                            for (T i = start; i < end; ++i)
                                (*loop_ptr)(i);
                        },
                        priority));
                }
                return future;
            }
            return {};
        }

        /**
         * @brief Submit a sequence of tasks enumerated by indices to the queue, with the specified priority. The sequence function takes one argument, the task index, and will be called once per index. Returns a `BS::multi_future` that contains the futures for all of the tasks.
         *
         * @tparam T1 The type of the first index. Should be a signed or unsigned integer.
         * @tparam T2 The type of the index after the last index. Should be a signed or unsigned integer.
         * @tparam F The type of the function used to define the sequence.
         * @tparam R The return type of the function used to define the sequence (can be `void`).
         * @param first_index The first index in the sequence.
         * @param index_after_last The index after the last index in the sequence. The sequence will iterate from `first_index` to `(index_after_last - 1)` inclusive. In other words, it will be equivalent to `for (T i = first_index; i < index_after_last; ++i)`. Note that if `index_after_last <= first_index`, no tasks will be submitted, and an empty `BS::multi_future` will be returned.
         * @param sequence The function used to define the sequence. Will be called once per index. Should take exactly one argument, the index.
         * @param priority The priority of the tasks. Should be between -128 and +127 (a signed 8-bit integer). The default is 0. Only taken into account if the flag `BS:tp::priority` is enabled in the template parameter, otherwise has no effect.
         * @return A `BS::multi_future` that can be used to wait for all the tasks to finish. If the sequence function returns a value, the `BS::multi_future` can also be used to obtain the values returned by each task.
         */
        template <typename T1, typename T2, typename T = common_index_type_t<T1, T2>, typename F, typename R = std::invoke_result_t<std::decay_t<F>, T>>
        [[nodiscard]] multi_future<R> submit_sequence(const T1 first_index, const T2 index_after_last, F&& sequence, const priority_t priority = 0)
        {
            if (static_cast<T>(index_after_last) > static_cast<T>(first_index))
            {
                const std::shared_ptr<std::decay_t<F>> sequence_ptr = std::make_shared<std::decay_t<F>>(std::forward<F>(sequence));
                multi_future<R> future;
                future.reserve(static_cast<std::size_t>(static_cast<T>(index_after_last) > static_cast<T>(first_index)));
                for (T i = static_cast<T>(first_index); i < static_cast<T>(index_after_last); ++i)
                {
                    future.push_back(submit_task(
                        [sequence_ptr, i]
                        {
                            return (*sequence_ptr)(i);
                        },
                        priority));
                }
                return future;
            }
            return {};
        }

        /**
         * @brief Submit a function with no arguments into the task queue, with the specified priority. To submit a function with arguments, enclose it in a lambda expression. If the function has a return value, get a future for the eventual returned value. If the function has no return value, get an `std::future<void>` which can be used to wait until the task finishes.
         *
         * @tparam F The type of the function.
         * @tparam R The return type of the function (can be `void`).
         * @param task The function to submit.
         * @param priority The priority of the task. Should be between -128 and +127 (a signed 8-bit integer). The default is 0. Only taken into account if the flag `BS:tp::priority` is enabled in the template parameter, otherwise has no effect.
         * @return A future to be used later to wait for the function to finish executing and/or obtain its returned value if it has one.
         */
        template <typename F, typename R = std::invoke_result_t<std::decay_t<F>>>
        [[nodiscard]] std::future<R> submit_task(F&& task, const priority_t priority = 0)
        {
    #ifdef __cpp_lib_move_only_function
            std::promise<R> promise;
        #define BS_THREAD_POOL_PROMISE_MEMBER_ACCESS promise.
    #else
            const std::shared_ptr<std::promise<R>> promise = std::make_shared<std::promise<R>>();
        #define BS_THREAD_POOL_PROMISE_MEMBER_ACCESS promise->
    #endif
            std::future<R> future = BS_THREAD_POOL_PROMISE_MEMBER_ACCESS get_future();
            detach_task(
                [task = std::forward<F>(task), promise = std::move(promise)]() mutable
                {
    #ifdef __cpp_exceptions
                    try
                    {
    #endif
                        if constexpr (std::is_void_v<R>)
                        {
                            task();
                            BS_THREAD_POOL_PROMISE_MEMBER_ACCESS set_value();
                        }
                        else
                        {
                            BS_THREAD_POOL_PROMISE_MEMBER_ACCESS set_value(task());
                        }
    #ifdef __cpp_exceptions
                    }
                    catch (...)
                    {
                        try
                        {
                            BS_THREAD_POOL_PROMISE_MEMBER_ACCESS set_exception(std::current_exception());
                        }
                        catch (...)
                        {
                        }
                    }
    #endif
                },
                priority);
            return future;
        }

        /**
         * @brief Unpause the pool. The workers will resume retrieving new tasks out of the queue. Only enabled if the flag `BS:tp::pause` is enabled in the template parameter.
         */
        BS_THREAD_POOL_IF_PAUSE_ENABLED
        void unpause()
        {
            {
                const std::scoped_lock tasks_lock(tasks_mutex);
                paused = false;
            }
            task_available_cv.notify_all();
        }

        /**
         * @brief Wait for tasks to be completed. Normally, this function waits for all tasks, both those that are currently running in the threads and those that are still waiting in the queue. However, if the pool is paused, this function only waits for the currently running tasks (otherwise it would wait forever). Note: To wait for just one specific task, use `submit_task()` instead, and call the `wait()` member function of the generated future.
         *
         * @throws `wait_deadlock` if called from within a thread of the same pool, which would result in a deadlock. Only enabled if the flag `BS:tp::wait_deadlock_checks` is enabled in the template parameter.
         */
        void wait()
        {
    #ifdef __cpp_exceptions
            if constexpr (wait_deadlock_checks_enabled)
            {
                if (this_thread::get_pool() == this)
                    throw wait_deadlock();
            }
    #endif
            std::unique_lock tasks_lock(tasks_mutex);
            waiting = true;
            tasks_done_cv.wait(tasks_lock,
                [this]
                {
                    if constexpr (pause_enabled)
                        return (tasks_running == 0) && (paused || tasks.empty());
                    else
                        return (tasks_running == 0) && tasks.empty();
                });
            waiting = false;
        }

        /**
         * @brief Wait for tasks to be completed, but stop waiting after the specified duration has passed.
         *
         * @tparam R An arithmetic type representing the number of ticks to wait.
         * @tparam P An `std::ratio` representing the length of each tick in seconds.
         * @param duration The amount of time to wait.
         * @return `true` if all tasks finished running, `false` if the duration expired but some tasks are still running.
         * @throws `wait_deadlock` if called from within a thread of the same pool, which would result in a deadlock. Only enabled if the flag `BS:tp::wait_deadlock_checks` is enabled in the template parameter.
         */
        template <typename R, typename P>
        bool wait_for(const std::chrono::duration<R, P>& duration)
        {
    #ifdef __cpp_exceptions
            if constexpr (wait_deadlock_checks_enabled)
            {
                if (this_thread::get_pool() == this)
                    throw wait_deadlock();
            }
    #endif
            std::unique_lock tasks_lock(tasks_mutex);
            waiting = true;
            const bool status = tasks_done_cv.wait_for(tasks_lock, duration,
                [this]
                {
                    if constexpr (pause_enabled)
                        return (tasks_running == 0) && (paused || tasks.empty());
                    else
                        return (tasks_running == 0) && tasks.empty();
                });
            waiting = false;
            return status;
        }

        /**
         * @brief Wait for tasks to be completed, but stop waiting after the specified time point has been reached.
         *
         * @tparam C The type of the clock used to measure time.
         * @tparam D An `std::chrono::duration` type used to indicate the time point.
         * @param timeout_time The time point at which to stop waiting.
         * @return `true` if all tasks finished running, `false` if the time point was reached but some tasks are still running.
         * @throws `wait_deadlock` if called from within a thread of the same pool, which would result in a deadlock. Only enabled if the flag `BS:tp::wait_deadlock_checks` is enabled in the template parameter.
         */
        template <typename C, typename D>
        bool wait_until(const std::chrono::time_point<C, D>& timeout_time)
        {
    #ifdef __cpp_exceptions
            if constexpr (wait_deadlock_checks_enabled)
            {
                if (this_thread::get_pool() == this)
                    throw wait_deadlock();
            }
    #endif
            std::unique_lock tasks_lock(tasks_mutex);
            waiting = true;
            const bool status = tasks_done_cv.wait_until(tasks_lock, timeout_time,
                [this]
                {
                    if constexpr (pause_enabled)
                        return (tasks_running == 0) && (paused || tasks.empty());
                    else
                        return (tasks_running == 0) && tasks.empty();
                });
            waiting = false;
            return status;
        }

    private:
        // ========================
        // Private member functions
        // ========================

        /**
         * @brief Create the threads in the pool and assign a worker to each thread.
         *
         * @param num_threads The number of threads to use.
         * @param init An initialization function to run in each thread before it starts executing any submitted tasks.
         */
        template <typename F>
        void create_threads(const std::size_t num_threads, F&& init)
        {
            if constexpr (std::is_invocable_v<F, std::size_t>)
            {
                init_func = std::forward<F>(init);
            }
            else
            {
                init_func = [init = std::forward<F>(init)](std::size_t)
                {
                    init();
                };
            }
            thread_count = determine_thread_count(num_threads);
            threads = std::make_unique<thread_t[]>(thread_count);
            {
                const std::scoped_lock tasks_lock(tasks_mutex);
                tasks_running = thread_count;
    #ifndef __cpp_lib_jthread
                workers_running = true;
    #endif
            }
            for (std::size_t i = 0; i < thread_count; ++i)
            {
                threads[i] = thread_t(
                    [this, i]
    #ifdef __cpp_lib_jthread
                    (const std::stop_token& stop_token)
                    {
                        worker(stop_token, i);
                    }
    #else
                    {
                        worker(i);
                    }
    #endif
                );
            }
        }

    #ifndef __cpp_lib_jthread
        /**
         * @brief Destroy the threads in the pool.
         */
        void destroy_threads()
        {
            {
                const std::scoped_lock tasks_lock(tasks_mutex);
                workers_running = false;
            }
            task_available_cv.notify_all();
            for (std::size_t i = 0; i < thread_count; ++i)
                threads[i].join();
        }
    #endif

        /**
         * @brief Determine how many threads the pool should have, based on the parameter passed to the constructor or reset().
         *
         * @param num_threads The parameter passed to the constructor or `reset()`. If the parameter is a positive number, then the pool will be created with this number of threads. If the parameter is non-positive, or a parameter was not supplied (in which case it will have the default value of 0), then the pool will be created with the total number of hardware threads available, as obtained from `thread_t::hardware_concurrency()`. If the latter returns zero for some reason, then the pool will be created with just one thread.
         * @return The number of threads to use for constructing the pool.
         */
        [[nodiscard]] static std::size_t determine_thread_count(const std::size_t num_threads) noexcept
        {
            if (num_threads > 0)
                return num_threads;
            if (thread_t::hardware_concurrency() > 0)
                return thread_t::hardware_concurrency();
            return 1;
        }

        /**
         * @brief A helper struct to store a task with an assigned priority.
         */
        struct [[nodiscard]] pr_task
        {
            /**
             * @brief Construct a new task with an assigned priority.
             *
             * @param task_ The task.
             * @param priority_ The desired priority.
             */
            explicit pr_task(task_t&& task_, const priority_t priority_ = 0) noexcept(std::is_nothrow_move_constructible_v<task_t>) : task(std::move(task_)), priority(priority_) {}

            /**
             * @brief Compare the priority of two tasks.
             *
             * @param lhs The first task.
             * @param rhs The second task.
             * @return `true` if the first task has a lower priority than the second task, `false` otherwise.
             */
            [[nodiscard]] friend bool operator<(const pr_task& lhs, const pr_task& rhs) noexcept
            {
                return lhs.priority < rhs.priority;
            }

            /**
             * @brief The task.
             */
            task_t task;

            /**
             * @brief The priority of the task.
             */
            priority_t priority = 0;
        }; // struct pr_task
        /**
         * @brief Pop a task from the queue.
         *
         * @return The task.
         */
        [[nodiscard]] task_t pop_task()
        {
            task_t task;
            if constexpr (priority_enabled)
                task = std::move(const_cast<pr_task&>(tasks.top()).task);
            else
                task = std::move(tasks.front());
            tasks.pop();
            return task;
        }

        /**
         * @brief Reset the pool with a new number of threads and a new initialization function. This member function implements the actual reset, while the public member function `reset()` also handles the case where the pool is paused.
         *
         * @param num_threads The number of threads to use.
         * @param init An initialization function to run in each thread before it starts executing any submitted tasks.
         */
        template <typename F>
        void reset_pool(const std::size_t num_threads, F&& init)
        {
            wait();
    #ifndef __cpp_lib_jthread
            destroy_threads();
    #endif
            create_threads(num_threads, std::forward<F>(init));
        }

        /**
         * @brief A worker function to be assigned to each thread in the pool. Waits until it is notified by `detach_task()` that a task is available, and then retrieves the task from the queue and executes it. Once the task finishes, the worker notifies `wait()` in case it is waiting.
         *
         * @param idx The index of this thread.
         */
        void worker(BS_THREAD_POOL_WORKER_TOKEN const std::size_t idx)
        {
            this_thread::my_pool = this;
            this_thread::my_index = idx;
            init_func(idx);
            while (true)
            {
                std::unique_lock tasks_lock(tasks_mutex);
                --tasks_running;
                if constexpr (pause_enabled)
                {
                    if (waiting && (tasks_running == 0) && (paused || tasks.empty()))
                        tasks_done_cv.notify_all();
                }
                else
                {
                    if (waiting && (tasks_running == 0) && tasks.empty())
                        tasks_done_cv.notify_all();
                }
                task_available_cv.wait(tasks_lock BS_THREAD_POOL_WAIT_TOKEN,
                    [this]
                    {
                        if constexpr (pause_enabled)
                            return !(paused || tasks.empty()) BS_THREAD_POOL_OR_STOP_CONDITION;
                        else
                            return !tasks.empty() BS_THREAD_POOL_OR_STOP_CONDITION;
                    });
                if (BS_THREAD_POOL_STOP_CONDITION)
                    break;
                {
                    task_t task = pop_task(); // NOLINT(misc-const-correctness) In C++23 this cannot be const since `std::move_only_function::operator()` is not a const member function.
                    ++tasks_running;
                    tasks_lock.unlock();
    #ifdef __cpp_exceptions
                    try
                    {
    #endif
                        task();
    #ifdef __cpp_exceptions
                    }
                    catch (...)
                    {
                    }
    #endif
                }
            }
            cleanup_func(idx);
            this_thread::my_index = std::nullopt;
            this_thread::my_pool = std::nullopt;
        }

        // ============
        // Private data
        // ============

        /**
         * @brief A cleanup function to run in each thread right before it is destroyed, which will happen when the pool is destructed or reset. The function must have no return value, and can either take one argument, the thread index of type `std::size_t`, or zero arguments. The cleanup function must not throw any exceptions, as that will result in program termination. Any exceptions must be handled explicitly within the function. The default is an empty function, i.e., no cleanup will be performed.
         */
        function_t<void(std::size_t)> cleanup_func = [](std::size_t) {};

        /**
         * @brief An initialization function to run in each thread before it starts executing any submitted tasks. The function must have no return value, and can either take one argument, the thread index of type `std::size_t`, or zero arguments. It will be executed exactly once per thread, when the thread is first constructed. The initialization function must not throw any exceptions, as that will result in program termination. Any exceptions must be handled explicitly within the function. The default is an empty function, i.e., no initialization will be performed.
         */
        function_t<void(std::size_t)> init_func = [](std::size_t) {};

        /**
         * @brief A flag indicating whether the workers should pause. When set to `true`, the workers temporarily stop retrieving new tasks out of the queue, although any tasks already executed will keep running until they are finished. When set to `false` again, the workers resume retrieving tasks. Only enabled if the flag `BS:tp::pause` is enabled in the template parameter.
         */
        std::conditional_t<pause_enabled, bool, std::monostate> paused = {};

    /**
     * @brief A condition variable to notify `worker()` that a new task has become available.
     */
    #ifdef __cpp_lib_jthread
        std::condition_variable_any
    #else
        std::condition_variable
    #endif
            task_available_cv;

        /**
         * @brief A condition variable to notify `wait()` that the tasks are done.
         */
        std::condition_variable tasks_done_cv;

        /**
         * @brief A queue of tasks to be executed by the threads.
         */
        std::conditional_t<priority_enabled, std::priority_queue<pr_task>, std::queue<task_t>> tasks;

        /**
         * @brief A mutex to synchronize access to the task queue by different threads.
         */
        mutable std::mutex tasks_mutex;

        /**
         * @brief A counter for the total number of currently running tasks.
         */
        std::size_t tasks_running = 0;

        /**
         * @brief The number of threads in the pool.
         */
        std::size_t thread_count = 0;

        /**
         * @brief A smart pointer to manage the memory allocated for the threads.
         */
        std::unique_ptr<thread_t[]> threads = nullptr;

        /**
         * @brief A flag indicating that `wait()` is active and expects to be notified whenever a task is done.
         */
        bool waiting = false;

    #ifndef __cpp_lib_jthread
        /**
         * @brief A flag indicating to the workers to keep running. When set to `false`, the workers terminate permanently.
         */
        bool workers_running = false;
    #endif
    }; // class thread_pool
}
#endif //THREAD_POOL_H
