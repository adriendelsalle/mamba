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

#include <reproc++/run.hpp>
#include <reproc/run.h>
#include <reproc/drain.h>
#include <reproc/reproc.h>
#include <cstring>

namespace mamba
{
    void shell(const std::string& action,
               std::string& shell_type,
               const std::string& prefix,
               bool stack,
               bool subshell)
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
            activator = std::make_unique<mamba::PosixActivator>(subshell);
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
            if (!fs::exists(shell_prefix))
            {
                throw std::runtime_error("Prefix does not exist");
            }

            if (subshell)
            {
                std::string tmp_rc = "umamba_session_rc";
                std::ifstream src("/home/adrien/.bashrc", std::ios::binary);
                std::ofstream dest(tmp_rc);
                dest << src.rdbuf();
                dest << R"MAMBARAW(
# Copyright (C) 2012 Anaconda, Inc
# SPDX-License-Identifier: BSD-3-Clause

__add_sys_prefix_to_path() {
    # In dev-mode MAMBA_EXE is python.exe and on Windows
    # it is in a different relative location to condabin.
    if [ -z "${MAMBA_ROOT_PREFIX}" ]; then
        return 0
    fi;

    if [ -n "${WINDIR+x}" ]; then
        PATH="${MAMBA_ROOT_PREFIX}/bin:${PATH}"
        PATH="${MAMBA_ROOT_PREFIX}/Scripts:${PATH}"
        PATH="${MAMBA_ROOT_PREFIX}/Library/bin:${PATH}"
        PATH="${MAMBA_ROOT_PREFIX}/Library/usr/bin:${PATH}"
        PATH="${MAMBA_ROOT_PREFIX}/Library/mingw-w64/bin:${PATH}"
        PATH="${MAMBA_ROOT_PREFIX}:${PATH}"
    else
        PATH="${MAMBA_ROOT_PREFIX}/bin:${PATH}"
    fi
    \export PATH
}


__conda_hashr() {
    if [ -n "${ZSH_VERSION:+x}" ]; then
        \rehash
    elif [ -n "${POSH_VERSION:+x}" ]; then
        :  # pass
    else
        \hash -r
    fi
}

__mamba_activate() {
    \local cmd="$1"
    shift
    \local ask_conda
    CONDA_INTERNAL_OLDPATH="${PATH}"
    __add_sys_prefix_to_path
    \local prefix="$1"
    shift
    if [ "$prefix" = "" ]; then
        prefix="base"
    fi
    ask_conda="$(PS1="$PS1" "$MAMBA_EXE" shell --shell bash "$cmd" --prefix "$prefix" $@)" || \return $?
    rc=$?
    PATH="${CONDA_INTERNAL_OLDPATH}"
    \eval "$ask_conda"
    if [ $rc != 0 ]; then
        \export PATH
    fi
    __conda_hashr
}

__mamba_reactivate() {
    \local ask_conda
    CONDA_INTERNAL_OLDPATH="${PATH}"
    __add_sys_prefix_to_path
    ask_conda="$(PS1="$PS1" "$MAMBA_EXE" shell --shell bash reactivate)" || \return $?
    PATH="${CONDA_INTERNAL_OLDPATH}"
    \eval "$ask_conda"
    __conda_hashr
}

micromamba() {
    if [ "$#" -lt 1 ]; then
        "$MAMBA_EXE"
    else
        \local cmd="$1"
        shift
        case "$cmd" in
            activate)
                __mamba_activate "$cmd" "$@"
                ;;
            deactivate)
                exit 2> /dev/null
                ;;
            install|update|upgrade|remove|uninstall)
                CONDA_INTERNAL_OLDPATH="${PATH}"
                __add_sys_prefix_to_path
                "$MAMBA_EXE" "$cmd" "$@"
                \local t1=$?
                PATH="${CONDA_INTERNAL_OLDPATH}"
                if [ $t1 = 0 ]; then
                    __mamba_reactivate
                else
                    return $t1
                fi
                ;;
            *)
                CONDA_INTERNAL_OLDPATH="${PATH}"
                __add_sys_prefix_to_path
                "$MAMBA_EXE" "$cmd" "$@"
                \local t1=$?
                PATH="${CONDA_INTERNAL_OLDPATH}"
                return $t1
                ;;
        esac
    fi
}

if [ -z "${CONDA_SHLVL+x}" ]; then
    \export CONDA_SHLVL=0
    # In dev-mode MAMBA_EXE is python.exe and on Windows
    # it is in a different relative location to condabin.
    if [ -n "${_CE_CONDA+x}" ] && [ -n "${WINDIR+x}" ]; then
        PATH="${MAMBA_ROOT_PREFIX}/condabin:${PATH}"
    else
        PATH="${MAMBA_ROOT_PREFIX}/condabin:${PATH}"
    fi
    \export PATH

    # We're not allowing PS1 to be unbound. It must at least be set.
    # However, we're not exporting it, which can cause problems when starting a second shell
    # via a first shell (i.e. starting zsh from bash).
    if [ -z "${PS1+x}" ]; then
        PS1=
    fi
fi
)MAMBARAW";
                dest << "micromamba activate " << shell_prefix;
                dest.close();

                //const std::vector<std::string> bash_args {"podman", "run", "-it", "-v", "/home/adrien:/test", "ubuntu:focal"};
                const std::vector<std::string> bash_args {"bash", "--rcfile", "umamba_session_rc", "-i"};
                std::vector<const char*> argv;

                for ( const auto& s : bash_args ) {
                    argv.push_back( s.data() );
                }
                argv.push_back(NULL);
                argv.shrink_to_fit();
                errno = 0;

                execv("/bin/bash", const_cast<char* const *>(argv.data()));
                //reproc_run(const_cast<char* const *>(argv.data()), (reproc_options) { 0 });

                std::cout << "deleting" << std::endl;
                fs::remove_all(tmp_rc);

            }
            
            std::cout << activator->activate(shell_prefix, stack);
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
