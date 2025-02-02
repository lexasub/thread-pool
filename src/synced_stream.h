#ifndef SYNCED_STREAM_H
#define SYNCED_STREAM_H
#include <iostream>
#include <mutex>
#include <ostream>
#include <vector>
#include "version.h"
/**
 * @brief A utility class to synchronize printing to an output stream by
 * different threads.
 */
namespace BS {
    class [[nodiscard]] synced_stream
    {
        public:
        /**
         * @brief Construct a new synced stream which prints to `std::cout`.
         */
        explicit synced_stream();

        /**
         * @brief Construct a new synced stream which prints to the given output
         * stream(s).
         *
         * @tparam T The types of the output streams to print to.
         * @param streams The output streams to print to.
         */
        template <typename... T>
        explicit synced_stream(T&... streams)
        {
            (add_stream(streams), ...);
        }

        /**
         * @brief Add a stream to the list of output streams to print to.
         *
         * @param stream The stream.
         */
        void add_stream(std::ostream &stream);

        /**
         * @brief Get a reference to a vector containing pointers to the output
         * streams to print to.
         *
         * @return The output streams.
         */
        std::vector<std::ostream *> &get_streams() noexcept;

        /**
         * @brief Print any number of items into the output stream. Ensures that no
         * other threads print to this stream simultaneously, as long as they all
         * exclusively use the same `BS::synced_stream` object to print.
         *
         * @tparam T The types of the items.
         * @param items The items to print.
         */
        template <typename... T>
        void print(const T&... items)
        {
            const std::scoped_lock stream_lock(stream_mutex);
            for (std::ostream* const stream : out_streams)
                (*stream << ... << items);
        }

        /**
         * @brief Print any number of items into the output stream, followed by a newline character. Ensures that no other threads print to this stream simultaneously, as long as they all exclusively use the same `BS::synced_stream` object to print.
         *
         * @tparam T The types of the items.
         * @param items The items to print.
         */
        template <typename... T>
        void println(T&&... items)
        {
            print(std::forward<T>(items)..., '\n');
        }

        /**
         * @brief Remove a stream from the list of output streams to print to.
         *
         * @param stream The stream.
         */
        void remove_stream(std::ostream &stream);

        /**
         * @brief A stream manipulator to pass to a `BS::synced_stream` (an explicit
         * cast of `std::endl`). Prints a newline character to the stream, and then
         * flushes it. Should only be used if flushing is desired, otherwise a
         * newline character should be used instead.
         */
        inline static std::ostream& (&endl)(std::ostream&) = static_cast<std::ostream& (&)(std::ostream&)>(std::endl);

        /**
         * @brief A stream manipulator to pass to a `BS::synced_stream` (an explicit cast of `std::flush`). Used to flush the stream.
         */
        inline static std::ostream& (&flush)(std::ostream&) = static_cast<std::ostream& (&)(std::ostream&)>(std::flush);

        private:
        /**
         * @brief The output streams to print to.
         */
        std::vector<std::ostream*> out_streams;

        /**
         * @brief A mutex to synchronize printing.
         */
        mutable std::mutex stream_mutex;
    }; // class synced_stream
}
extern BS::synced_stream sync_out;
#endif //SYNCED_STREAM_H
