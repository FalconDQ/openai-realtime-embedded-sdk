# Local credentials

The root `.env` is loaded automatically during CMake configuration, including
`idf.py set-target esp32s3`, IDE builds, and normal `idf.py build`.
Python from the ESP-IDF environment is used; no additional package is required.

Create `.env` locally from `.env.example` and enter your values yourself.
Never commit `.env`. Process environment variables override matching `.env`
entries, so the existing CI dummy environment still works.

Supported syntax: one `NAME=value` per line, optional `export`, blank lines,
comments beginning with `#`, and single/double-quoted values. Quotes preserve
spaces and `#`; unquoted inline comments require whitespace before `#`.
Values are literal: no variable expansion, shell commands, multiline values,
or backslash escape processing. Use the other quote style if a value contains
a quote. Duplicate known keys and missing/empty required values are errors.
Linux builds require only OPENAI_API_KEY; device builds also require both Wi-Fi
values. Empty Wi-Fi passwords (open networks) are not supported by this loader.

## What is protected, and what is not

Credentials are no longer passed as compiler `-D` options. Their values are
not written into CMake cache entries, compile_commands.json or build.ninja
by this integration. Errors report names/line numbers, never values.
The generated private header is in `build/local_config/local_secrets.h` and
uses hex-escaped bytes to prevent literal secret text in source diagnostics.
Hex escaping is NOT encryption. Application logging no longer prints the
Wi-Fi SSID or HTTP response header values.

The header, object files, ELF, firmware binaries, and compiler caches still
contain recoverable credentials. Never upload these artifacts or caches from
a real-credential build. Avoid CMake expanded tracing and low-level HTTP/Wi-Fi
verbose/debug dumps, which may expose runtime buffers. Gitignore is an
accident-prevention measure, not encryption and not protection against force-add.
Any already tracked secrets must be removed from tracking; if published, revoke
the API key and change the affected password. This change does not erase past logs.

This remains build-time credential injection, not encrypted NVS provisioning.
A firmware binary must be treated as secret. To distribute firmware safely,
use a separate device provisioning flow and preferably short-lived API tokens
issued by an authenticated backend. Those changes are outside this patch.

## Build

Select ESP-IDF and target esp32s3 in VS Code, then build normally. Updating `.env`
triggers CMake regeneration on the next build. Switching targets with fullclean
regenerates the private header. If credentials are supplied only through process
environment variables and change, run `idf.py reconfigure` before building again.

For a custom build directory, keep the entire directory out of Git and any
artifact upload. Ignore rules also exclude the generated header by filename.
Do not add actual credentials to VS Code settings or command-line arguments.
