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
    cargo quickinstall hyperfine

parallel-bash-commands:
    echo 'pids=()'
    -rg '^(.*)$' --replace '$1 &'$'\n''pids+=($!)' --color never
    echo 'for pid in ${pids[@]}; do wait $pid; done'

sequential-bash:
    bash

parallel-bash:
    just parallel-bash-commands | bash

map-lines pattern replacement:
    -rg '^{{pattern}}$' --replace '{{replacement}}' --color never

lookup-uni name:
    curl "https://directory.columbia.edu/people/search?filter.searchTerm={{replace(name, " ", "+")}}" \
        2> /dev/null \
        | rg '\?code=([a-zA-Z0-9]+)' --only-matching --replace '$1'

get-git-uni:
    just lookup-uni "$(git config user.name)"

pre-make:

make-with-makefile path *args:
    cd "{{parent_directory(path)}}" && make --makefile "{{file_name(path)}}" -j{{n_proc}} {{args}}

make-in dir *args: (make-with-makefile join(dir, "Makefile") args)

make-in-recursive dir *args:
    fd '^Makefile$' "{{dir}}" \
        | just map-lines '(.*)' 'just make-with-makefile "$1" {{args}}' \
        | just parallel-bash

make-lib *args: (make-in-recursive "user/lib" args)

make-mods *args: (make-in-recursive "user/module" args)

make-test *args: (make-in-recursive "user/test" args)

make-user *args: (make-in-recursive "user/" args)

make-non-kernel *args: (make-user args)

make-kernel *args: (make-in "linux" args)

modify-config:
    ./linux/scripts/config --file linux/.config \
        --set-str LOCALVERSION "\"-$(just get-git-uni)-fridge\"" \
        --enable BLK_DEV_LOOP \
        --set-val SYSTEM_TRUSTED_KEYS '' \
        --enable STACKTRACE \
        --enable KASAN \
        --enable KASAN_GENERIC \
        --enable KASAN_INLINE \
        --disable KASAN_VMALLOC \
        --disable TEST_KASAN_MODULE \
        --enable UBSAN \
        --disable UBSAN_TRAP \
        --enable UBSAN_BOUNDS \
        --enable UBSAN_MISC \
        --disable UBSAN_SANITIZE_ALL \
        --disable UBSAN_ALIGNMENT \
        --disable TEST_UBSAN \
        --disable RANDOMIZE_BASE

generate-config: && modify-config
    yes '' | just make-kernel localmodconfig

apply-fridge-patch:
    git apply patch/fridge.patch

setup-kernel: (make-kernel "mrproper") generate-config make-kernel install-kernel
    @echo now reboot

install-fridge:
    sudo -E env "PATH=${PATH}" just make-fridge install

git-clone repo *args=("--recursive"):
    cd "{{invocation_directory()}}" && \
        command -v gh >/dev/null \
        && gh repo clone "{{repo}}" -- {{args}} \
        || git clone "https://github.com/{{repo}}" {{args}}

reinstall-kedr:
    #!/usr/bin/env bash
    set -euxo pipefail

    rm -rf ~/kedr
    just git-clone hmontero1205/kedr ~/kedr
    cd ~/kedr/sources
    mkdir build
    cd build
    cmake ..
    make -j{{n_proc}}
    sudo make install

# Paranthesized deps to avoid checkpatch repeated word warning
make: (pre-make) (make-non-kernel) (make-kernel)

install-kernel-no-sudo: (make-kernel "modules_install") (make-kernel "install")

install-kernel: make-kernel
    sudo -E env "PATH=${PATH}" just install-kernel-no-sudo

first-commit:
    git rev-list --max-parents=0 HEAD

modified-files *args:
    git diff --name-only {{args}}

# clang-format formats some stuff wrong, so re-format it
# specifically for each macros
refmt *paths:
    #!/usr/bin/env node
    const fsp = require("fs/promises");
    const pathLib = require("path");

    async function reformatCFile(path) {
        let s = await fsp.readFile(path);
        s = s.toString();
        const original = s;
        // fix for each macros
        s = s.replaceAll(/([a-z_]*for_each[a-z_]*) /g, (_, macroName) => macroName);
        const to = s;
        await fsp.writeFile(path, s);
        return s !== original;
    }

    async function main() {
        // const [_node, _program, ...paths] = process.argv;
        const pathsGiven = `{{paths}}`.split(" ");
        let it = pathsGiven;
        it = it.filter(path => ["c", "h"].includes(pathLib.extname(path).slice(1)));
        it = [...new Set(it)];
        const pathsFormatting = it;
        console.log(["reformatting:", ...pathsFormatting].join("\n\t"));
        it = it.map(async (path, i) => ({path, formatted: await reformatCFile(path)}));
        it = await Promise.all(it);
        const pathsFormatted = it.filter(e => e.formatted).map(e => e.path);
        console.log(["reformatted:", ...pathsFormatted].join("\n\t"));
    }

    main().catch(e => {
        console.error(e);
        process.exit(1);
    });

fmt *args:
    ln --symbolic --force linux/.clang-format .
    git clang-format --force "$(just first-commit)" {{args}}
    just refmt $(just modified-files "$(just first-commit)")

entire-diff *args:
    git diff "$(just first-commit)" {{args}}

pre-commit-fast: fmt check-patch

pre-commit-slow: make

pre-commit: pre-commit-fast pre-commit-slow

gitui: pre-commit
    gitui

compile-commands-non-kernel: (make-non-kernel "clean")
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

compile-commands: compile-commands-non-kernel compile-commands-kernel (join-compile-commands "user")

log *args:
    sudo dmesg --kernel --reltime {{args}}

log-watch *args: (log "--follow-new" args)

run-mod mod_path *args:
    #!/usr/bin/env bash
    just log | wc -l > log.length
    sudo kedr start "{{mod_path}}"
    echo "running $(tput setaf 2){{file_stem(mod_path)}}$(tput sgr 0):"
    just load-mod "{{mod_path}}"
    {{args}}
    just unload-mod-by-path "{{mod_path}}"
    just log --color=always | tail -n "+$(($(cat log.length) + 1))"
    rm log.length
    exit
    cd /sys/kernel/debug/kedr_leak_check
    bat --paging never info possible_leaks unallocated_frees
    sudo kedr stop

test *args: (run-mod default_mod_path args)

test-part-raw part_name *args: (make-in "user/test/FireFerrises-p" + part_name + "-test" args)

test-part part_name: (test "just" "test-part-raw" part_name "test-all")

benchmark branch:
    git checkout "{{branch}}"
    just make-mods
    just load-mod
    hyperfine 'just test-part-raw 3 test'
    just unload-mod
    git checkout -

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

default_mod_path := "user/module/fridge/fridge.ko"

is-mod-loaded name=file_stem(default_mod_path):
    rg --quiet '^{{name}} ' /proc/modules

# `path` is `path_` instead to appease checkpatch's repeated word warning
is-mod-loaded-by-path path_=default_mod_path: (is-mod-loaded file_stem(path_))

load-mod path=default_mod_path:
    just unload-mod-by-path "{{path}}"
    sudo insmod "{{path}}"

unload-mod name=file_stem(default_mod_path):
    just is-mod-loaded "{{name}}" 2> /dev/null && sudo rmmod "{{name}}" || true

# `path` is `path_` instead to appease checkpatch's repeated word warning
unload-mod-by-path path_=default_mod_path: (unload-mod file_stem(path_))

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

rename-branch old_name new_name:
    git checkout "{{old_name}}"
    git branch --move "{{new_name}}"
    git push origin --set-upstream "{{new_name}}"
    git push origin --delete "{{old_name}}"
    git checkout -
