#ifndef ARG_PARSER_H
#define ARG_PARSER_H
#include <map>
#include <set>
#include <string_view>

/**
 * @brief A class to parse command line arguments. All arguments are assumed to be on/off and default to off.
 */
class [[nodiscard]] arg_parser
{
public:
    /**
     * @brief Convert the command line arguments passed to the `main()` function into an `std::vector`.
     *
     * @param argc The number of arguments.
     * @param argv An array containing the arguments.
     */
    arg_parser(int argc, char* argv[]) : args(argv + 1, argv + argc), executable(argv[0]) {};

    /**
     * @brief Check if a specific command line argument has been passed to the program. If no arguments were passed, use the default value instead.
     *
     * @param arg The argument to check for.
     * @return `true` if the argument exists, `false` otherwise.
     */
    [[nodiscard]] bool operator[](const std::string_view arg);

    /**
     * @brief Add an argument to the list of allowed arguments.
     *
     * @param arg The argument.
     * @param desc The description of the argument.
     * @param def The default value of the argument.
     */
    void add_argument(const std::string_view arg, const std::string_view desc,
                      const bool def);

    /**
     * @brief Get the name of the executable.
     *
     * @return The name of the executable.
     */
    std::string_view get_executable() const;

    void show_help() const;

    /**
     * @brief Get the number of command line arguments.
     *
     * @return The number of arguments.
     */
    [[nodiscard]] std::size_t size() const;

    /**
     * @brief Verify that the command line arguments belong to the list of
     * allowed arguments.
     *
     * @return `true` if all arguments are allowed, `false` otherwise.
     */
    [[nodiscard]] bool verify() const;

  private:
    struct arg_spec
    {
        std::string_view desc;
        bool def = false;
    };

    /**
     * @brief A set containing string views of the command line arguments.
     */
    std::set<std::string_view> args;

    /**
     * @brief A map containing the allowed arguments and their descriptions.
     */
    std::map<std::string_view, arg_spec> allowed;

    /**
     * @brief A string view containing the name of the executable.
     */
    std::string_view executable;
}; // class arg_parser


#endif //ARG_PARSER_H
