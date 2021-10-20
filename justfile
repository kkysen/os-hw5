set shell := ["bash", "-c"]

hw := "5"
n_proc := `nproc`

default:
    just --list --unsorted

install-program-dependencies:
    sudo apt install \
        clang-format \
        bear \
        make \
        build-essential \
        bc \
        python \
        bison \
        flex \
        libelf-dev \
        libssl-dev \
        libncurses-dev \
        dwarves
    command -v cargo > /dev/null || curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
    cargo install cargo-quickinstall
    cargo quickinstall just
    cargo quickinstall ripgrep
    cargo quickinstall fd-find
    cargo quickinstall sd
    cargo quickinstall gitui

parallel-bash-commands:
    -rg '^(.*)$' --replace '$1 &' --color never
    echo wait

sequential-bash:
    bash

parallel-bash:
    just parallel-bash-commands | bash

map-lines pattern replacement:
    -rg '^{{pattern}}$' --replace '{{replacement}}' --color never

pre-make:

make-in dir *args:
    cd "{{dir}}" && make -j{{n_proc}} {{args}}

make-test *args: (make-in "user/test" args)

make-mod *args:
    #(make-in "user/module/supermom" args)

make-non-kernel *args: (make-test args) (make-mod args)

make-kernel *args: (make-in "linux" args)

#make-sys-supermom *args: (make-kernel "kernel/supermom.o" args)

# Paranthesized deps to avoid checkpatch repeated word warning
make: (pre-make) (make-mod) (make-kernel)

install-only: (make-kernel "modules_install") (make-kernel "install")

install: make-kernel
    sudo -E env "PATH=${PATH}" just install-only

modified-files:
    git diff --name-only

fmt-commands *args:
    just modified-files | rg '\.(c|h)$' \
        | just map-lines '(.*)' 'clang-format {{args}} "$1"'

fmt-args *args:
    ln --symbolic --force linux/.clang-format .
    just fmt-commands {{args}} | just parallel-bash

fmt: (fmt-args "-i")

pre-commit-fast: fmt check-patch

pre-commit-slow: make

pre-commit: pre-commit-fast pre-commit-slow

gitui: pre-commit
    gitui

compile-commands-mod: (make-non-kernel "clean")
    cd user && bear -- just make-non-kernel
    command -v ccache > /dev/null \
        && sd "$(which ccache)" "$(which gcc)" user/compile_commands.json \
        || true

compile-commands-kernel:
    cd linux && ./scripts/clang-tools/gen_compile_commands.py

join-compile-commands *dirs:
    #!/usr/bin/env node
    const fsp = require("fs/promises");
    const pathLib = require("path");

    function openCompileCommands(dir) {
        const path = pathLib.join(dir, "compile_commands.json");
        return {
            async read() {
                const json = await fsp.readFile(path, "utf-8");
                return JSON.parse(json);
            },
            async write(compileCommands) {
                const json = JSON.stringify(compileCommands, null, 4);
                await fsp.writeFile(path, json);
            }
        };
    }

    async function main() {
        const dirs = "{{dirs}}".split(" ");
        const compileCommandArrays = await Promise.all(dirs.map(dir => openCompileCommands(dir).read()));
        const joinedCompileCommands = compileCommandArrays.flat();
        await openCompileCommands(".").write(joinedCompileCommands);
    }

    main().catch(e => {
        console.error(e);
        process.exit(1);
    });

compile-commands: compile-commands-mod compile-commands-kernel (join-compile-commands "user" "linux")

log *args:
    sudo dmesg --kernel --reltime {{args}}

log-watch *args: (log "--follow-new" args)

run-mod-priv dir:
    #!/usr/bin/env bash
    cd "{{dir}}"
    just log | wc -l > log.length
    #kedr start *.ko
    echo "running $(tput setaf 2){{dir}}$(tput sgr 0):"
    insmod *.ko
    rmmod *.ko
    just log --color=always | tail -n "+$(($(cat log.length) + 1))"
    rm log.length
    exit
    cd /sys/kernel/debug/kedr_leak_check
    bat --paging never info possible_leaks unallocated_frees
    kedr stop

run-mod-only dir:
    sudo env "PATH=${PATH}:/usr/local/sbin:/usr/sbin:/sbin" just run-mod-priv "{{dir}}"

run-mod dir: (run-mod-only dir)

# setup-kernel:
#     make -C linux mrproper
#     make -C linux olddefconfig
#     make -C linux menuconfig
#     ./linux/scripts/diffconfig

default-branch:
    git remote show origin | rg 'HEAD branch: (.*)$' --only-matching --replace '$1'

tag name message:
    git tag -a -m "{{message}}" "{{name}}"
    git push origin "$(just default-branch)"
    git push origin "{{name}}"

untag name:
    git push --delete origin "{{name}}"
    git tag --delete "{{name}}"

submit part: (tag "hw" + hw + "p" + part + "handin" "Completed hw" + hw + " part" + part + ".")

unsubmit part: (untag "hw" + hw + "p" + part + "handin")

diff-command:
    command -v delta > /dev/null && echo delta || echo diff

diff a b:
    "$(just diff-command)" "{{a}}" "{{b}}"

test: make-test
    just log --color=never | wc -l > log.$PPID.length
    -just make-test run --silent > expected.$PPID.log
    just log --color=never \
        | rg '^\[[^\]]*\] (.*)$' --replace '$1' \
        | tail -n "+$(($(cat log.$PPID.length) + 1))" \
        > actual.$PPID.log
    rm log.$PPID.length
    -just diff {expected,actual}.$PPID.log
    rm {expected,actual}.$PPID.log

is-mod-loaded name:
    rg --quiet '^{{name}} ' /proc/modules

# `path` is `path_` instead to appease checkpatch's repeated word warning
is-mod-loaded-by-path path_: (is-mod-loaded file_stem(path_))

load-mod path:
    just unload-mod-by-path "{{path}}"
    sudo insmod "{{path}}"

unload-mod name:
    just is-mod-loaded "{{name}}" 2> /dev/null && sudo rmmod "{{name}}" || true

# `path` is `path_` instead to appease checkpatch's repeated word warning
unload-mod-by-path path_: (unload-mod file_stem(path_))

check-patch:
    ./run_checkpatch.sh

filter-exec:
    #!/usr/bin/env node
    const fs = require("fs");
    const pathLib = require("path");

    const which = (() => {
        const cache = new Map();
        const paths = process.env.PATH.split(pathLib.delimiter);
        return program => {
            if (cache.has(program)) {
                return cache.get(program);
            }
            for (const dir of paths) {
                const path = pathLib.join(dir, program);
                let found = false;
                try {
                    fs.accessSync(path, fs.constants.X_OK);
                    found = true;
                } catch {}
                if (found) {
                    cache.set(program, path);
                    return path;
                }
            }
            cache.set(program, undefined);
            return undefined;
        };
    })();

    const colors = {
        reset: 0,
        bright: 1,
        dim: 2,
        underscore: 4,
        blink: 5,
        reverse: 7,
        hidden: 8,
        fg: {
            black: 30,
            red: 31,
            green: 32,
            yellow: 33,
            blue: 34,
            magenta: 35,
            cyan: 36,
            white: 37,
        },
        bg: {
            black: 40,
            red: 41,
            green: 42,
            yellow: 43,
            blue: 44,
            magenta: 45,
            cyan: 46,
            white: 47,
        },
    };

    function ansiColorSequence(colorCode) {
        return `\x1b[${colorCode}m`;
    }

    const color = !process.stdout.isTTY ? ((colorCode, s) => s) : (colorCode, s) => {
        if (colorCode === undefined) {
            throw new Error("undefined color");
        }
        return ansiColorSequence(colorCode) + s + ansiColorSequence(colors.reset);
    };

    function quote(s) {
        return s.includes(" ") ? `"${s}"` : s;
    }

    function colorPath(path, dirColor, nameColor) {
        const {dir, base} = pathLib.parse(path);
        return (dir ? color(dirColor, dir + pathLib.sep) : "") + color(nameColor, base);
    }

    const output = fs.readFileSync("/dev/stdin")
        .toString()
        .split("\n")
        .map(s => {
            const match = /execve\(([^)]*)\) = 0/.exec(s);
            if (!match) {
                return;
            }
            const [_, argsStr] = match;
            const args = argsStr.replaceAll(/\[|\]/g, "").split(", ");
            return args
                .map(rawArgs => {
                    if (!(rawArgs.endsWith('"') && rawArgs.endsWith('"'))) {
                        return;
                    }
                    const arg = rawArgs.slice('"'.length, -'"'.length);
                    return arg;
                })
                .filter(Boolean);
        })
        .filter(Boolean)
        .map(([path, argv0, ...argv]) => {
            const program = pathLib.basename(path);
            const isInPath = which(program) === path;
            return {
                path: isInPath ? program : path,
                argv0: (argv0 === path || argv0 === program) ? undefined : argv0,
                argv,
            };
        }).map(({path, argv0, argv}) => ({
            path: quote(path),
            argv0: argv0 === undefined ? undefined : quote(argv0),
            argv: argv.map(quote),
        })).map(({path, argv0, argv}) => {
            if (argv0 === undefined) {
                return [
                    colorPath(path, colors.fg.yellow, colors.fg.green),
                    ...argv,
                ];
            } else {
                return [
                    "[" + colorPath(path, colors.fg.yellow, colors.fg.blue) + "]",
                    colorPath(argv0, colors.fg.yellow, colors.fg.green),
                    ...argv,
                ];
            }
        })
        .map(args => args.join(" "))
        .join("\n") + "\n";
    fs.writeFileSync("/dev/stdout", output);

trace-exec *args:
    -strace -etrace=execve -f --string-limit 10000 -qq --output strace.$PPID.out {{args}}
    just filter-exec < strace.$PPID.out
    rm strace.$PPID.out
