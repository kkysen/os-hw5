#!/usr/bin/env bash

is-command() {
    command -v "$1" > /dev/null
}

is-command curl || sudo apt install curl
is-command cargo || curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
