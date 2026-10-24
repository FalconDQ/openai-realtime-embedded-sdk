"""Generate a private build header; never send credential values to CMake."""
import argparse
import os
from pathlib import Path
import re
import sys

KEYS = ("WIFI_SSID", "WIFI_PASSWORD", "OPENAI_API_KEY")


class ConfigError(Exception):
    pass


def read_dotenv(path):
    values = {}
    if not path.exists():
        return values
    for number, raw in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("export "):
            line = line[7:].lstrip()
        key, separator, value = line.partition("=")
        key = key.strip()
        if key not in KEYS:
            continue
        if not separator:
            raise ConfigError(f"Invalid assignment for {key} at line {number}.")
        value = value.strip()
        if value.startswith(("'", '"')):
            quote = value[0]
            end = value.find(quote, 1)
            if end < 0 or (value[end + 1:].strip() and not value[end + 1:].lstrip().startswith("#")):
                raise ConfigError(f"Invalid quoting for {key} at line {number}.")
            value = value[1:end]
        else:
            value = re.split(r"\s+#", value, maxsplit=1)[0].rstrip()
        if key in values:
            raise ConfigError(f"Duplicate assignment for {key}.")
        values[key] = value
    return values


def generate(dotenv, output, target, environment=None):
    environment = os.environ if environment is None else environment
    values = read_dotenv(dotenv)
    # Explicit process environment overrides .env, preserving the CI interface.
    for key in KEYS:
        if key in environment:
            values[key] = environment[key]
    required = ("OPENAI_API_KEY",) if target == "linux" else KEYS
    for key in required:
        if not values.get(key):
            raise ConfigError(f"Missing {key}: configure it in the root .env or process environment.")
        if any(c in values[key] for c in ("\0", "\r", "\n")):
            raise ConfigError(f"Unsupported control character in {key}.")
    if target != "linux":
        if len(values["WIFI_SSID"].encode("utf-8")) > 32:
            raise ConfigError("WIFI_SSID exceeds the ESP32's 32-byte limit.")
        if len(values["WIFI_PASSWORD"].encode("utf-8")) > 64:
            raise ConfigError("WIFI_PASSWORD exceeds the ESP32's 64-byte limit.")
    content = "// Generated locally. Contains secrets. Never publish this file.\n#pragma once\n"
    for key in required:
        # Encode bytes to keep quotes/backslashes safe and avoid plaintext in
        # compiler source-line diagnostics. This is NOT encryption.
        literal = "".join(f"\\x{byte:02x}" for byte in values[key].encode("utf-8"))
        content += f'#define {key} "{literal}"\n'
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding="utf-8") != content:
        output.write_text(content, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dotenv", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target", default="esp32s3")
    args = parser.parse_args()
    try:
        generate(args.dotenv, args.output, args.target)
    except ConfigError as error:
        print(f"Local configuration: {error}", file=sys.stderr)
        return 1
    except Exception:
        # No traceback: parser/I/O exceptions could otherwise disclose input.
        print("Local configuration: unable to read .env or write the private header.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
