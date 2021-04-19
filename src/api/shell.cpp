// Copyright (c) 2019, QuantStack and Mamba Contributors
//
// Distributed under the terms of the BSD 3-Clause License.
//
// The full license is in the file LICENSE, distributed with this software.

#include "mamba/api/configuration.hpp"
#include "mamba/api/shell.hpp"

#include "mamba/core/activation.hpp"
#include "mamba/core/context.hpp"
#include "mamba/core/environment.hpp"
#include "mamba/core/mamba_fs.hpp"
#include "mamba/core/shell_init.hpp"


namespace mamba
{
    void shell(const std::string& action,
               std::string& shell_type,
               const std::string& prefix,
               bool stack,
               bool isolated)
    {
        auto& ctx = Context::instance();
        auto& config = Configuration::instance();

        config.at("show_banner").set_value(false);
        config.at("use_target_prefix_fallback").set_value(true);
        config.at("target_prefix_checks")
            .set_value(MAMBA_ALLOW_EXISTING_PREFIX | MAMBA_ALLOW_MISSING_PREFIX);
        config.load();

        if (shell_type.empty())
        {
            LOG_DEBUG << "No shell type provided";

            std::string guessed_shell = guess_shell();
            if (!guessed_shell.empty())
            {
                LOG_DEBUG << "Guessed shell: '" << guessed_shell << "'";
                shell_type = guessed_shell;
            }

            if (shell_type.empty())
            {
                std::cout << "Please provide a shell type." << std::endl;
                std::cout << "Run with --help for more information." << std::endl;
                return;
            }
        }

        std::string shell_prefix = env::expand_user(prefix);
        std::unique_ptr<Activator> activator;

        if (shell_type == "bash" || shell_type == "zsh" || shell_type == "posix")
        {
            activator = std::make_unique<mamba::PosixActivator>();
        }
        else if (shell_type == "cmd.exe")
        {
            activator = std::make_unique<mamba::CmdExeActivator>();
        }
        else if (shell_type == "powershell")
        {
            activator = std::make_unique<mamba::PowerShellActivator>();
        }
        else if (shell_type == "xonsh")
        {
            activator = std::make_unique<mamba::XonshActivator>();
        }
        else
        {
            LOG_ERROR << "Not handled 'shell_type': " << shell_type;
            return;
        }

        if (action == "init")
        {
            if (shell_prefix == "base")
            {
                shell_prefix = ctx.root_prefix;
            }
            init_shell(shell_type, shell_prefix);
        }
        else if (action == "hook")
        {
            // TODO do we need to do something wtih `shell_prefix -> root_prefix?`?
            if (ctx.json)
            {
                JsonLogger::instance().json_write(
                    { { "success", true },
                      { "operation", "shell_hook" },
                      { "context", { { "shell_type", shell_type } } },
                      { "actions", { { "print", { activator->hook() } } } } });
                if (Context::instance().json)
                    Console::instance().print(JsonLogger::instance().json_log.unflatten().dump(4),
                                              true);
            }
            else
            {
                std::cout << activator->hook();
            }
        }
        else if (action == "activate")
        {
            if (shell_prefix == "base" || shell_prefix.empty())
            {
                shell_prefix = ctx.root_prefix;
            }
            if (shell_prefix.find_first_of("/\\") == std::string::npos)
            {
                shell_prefix = ctx.root_prefix / "envs" / shell_prefix;
            }

            if (isolated)
            {
                if (!config.at("experimental").value<bool>())
                {
                    LOG_ERROR << "Shell activation using container isolation needs 'experimental' mode";
                    return;
                }
                if (!on_linux)
                {
                    LOG_ERROR << "Shell activation using container isolation is only available on linux for now";
                    return;
                }


            }
            else
            {
                std::cout << activator->activate(shell_prefix, stack);
            }
        }
        else if (action == "reactivate")
        {
            std::cout << activator->reactivate();
        }
        else if (action == "deactivate")
        {
            std::cout << activator->deactivate();
        }
        else
        {
            throw std::runtime_error("Need an action (activate, deactivate or hook)");
        }

        config.operation_teardown();
    }
}  // mamba
