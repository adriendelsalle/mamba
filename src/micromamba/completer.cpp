#include "mamba/api/configuration.hpp"

#include "mamba/core/environments_manager.hpp"
#include "mamba/core/output.hpp"


std::vector<std::string> first_level_completions = { "activate", "deactivate", "install",
                                                     "remove",   "update",     "list",
                                                     "clean",    "config",     "info" };

bool
string_comparison(const std::string& a, const std::string& b)
{
    return a < b;
}

void
complete_options(CLI::App* app, const std::string& last_arg, bool& completed)
{
    if (completed)
        return;
    completed = true;

    if (last_arg == "-n")
    {
        auto& config = mamba::Configuration::instance();
        config.at("show_banner").set_value(false);
        config.load();

        auto root_prefix = config.at("root_prefix").value<fs::path>();

        std::vector<std::string> prefixes;
        if (fs::exists(root_prefix / "envs"))
            for (const auto& p : fs::directory_iterator(root_prefix / "envs"))
                if (p.is_directory() && fs::exists(p / "conda-meta"))
                    prefixes.push_back(p.path().filename().string());

        std::sort(prefixes.begin(), prefixes.end(), string_comparison);
        std::cout << mamba::printers::table_like(prefixes, 90).str() << std::endl;
    }
    else if (mamba::starts_with(last_arg, "-"))
    {
        auto opt_start = mamba::lstrip(last_arg, "-");

        if (opt_start.empty())
            return;

        std::vector<std::string> options;
        if (mamba::starts_with(last_arg, "--"))
            for (const auto* opt : app->get_options())
            {
                for (const auto& n : opt->get_lnames())
                    if (mamba::starts_with(n, opt_start))
                        options.push_back("--" + n);
            }
        else
            for (const auto* opt : app->get_options())
            {
                for (const auto& n : opt->get_snames())
                    if (mamba::starts_with(n, opt_start))
                        options.push_back("-" + n);
            }

        // std::cout << mamba::join(" ", options) << std::endl;
        std::cout << mamba::printers::table_like(options, 90).str() << std::endl;
    }
}

int
get_completions(CLI::App* app, int argc, char** argv)
{
    bool completed = false;
    CLI::App* install_subcom = app->get_subcommand("install");

    app->callback([&]() { complete_options(app, argv[argc - 1], completed); });
    install_subcom->callback(
        [&]() { complete_options(install_subcom, argv[argc - 1], completed); });

    argv[1] = argv[0];
    CLI11_PARSE(*app, argc - 2, argv + 1);

    exit(0);
}
