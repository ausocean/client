import json
import subprocess
import sys
from typing import Any


def astyle_format_file(filepath: str) -> None:
    """
    Formats the specified filepath using the astyle settings used for c/c++ files.
    """
    # Astyle command used to format c/c++ files.
    command: list[str] = [
        "astyle",
        "--style=otbs",
        "--attach-namespaces",
        "--attach-classes",
        "--indent=spaces=4",
        "--convert-tabs",
        "--align-reference=name",
        "--keep-one-line-statements",
        "--pad-header",
        "--pad-oper",
        "--unpad-paren",
        "--max-continuation-indent=120",
        "-n",
        filepath,
    ]

    # Run formatting.
    try:
        subprocess.run(command, check=True, capture_output=True, text=True)
        print(f"Successfully formatted {filepath}")
    except subprocess.CalledProcessError as e:
        print(f"Error formatting file: {e.stderr}")


def generate_header(json_path: str, output_path: str) -> None:
    """
    Generates a C++ header file (.hpp) from the passed JSON file, and puts it in the specified output path.
    """

    class VarType:
        BYTE: str = "byte"
        STRING: str = "string"

    # Map JSON types to enum values.
    ENUM_TYPE_MAP: dict[str, str] = {VarType.BYTE: "BYTE", VarType.STRING: "STRING"}

    # Map JSON types to C types.
    C_TYPE_MAP: dict[str, str] = {VarType.BYTE: "char", VarType.STRING: "char*"}

    # Open the JSON file.
    with open(json_path, "r") as f:
        data: dict[str, Any] = json.load(f)

    # Extract the client.
    client_name: str = list(data.keys())[0]

    # Generate header based off most recent version.
    latest_version: str = list(data[client_name])[-1]

    # Get the registered variables.
    vars: list[dict[str, str]] = data[client_name][latest_version]["variables"]
    var_count: int = len(vars)

    with open(output_path, "w") as f:
        # Add includes.
        f.write(
            "#pragma once\n#include <array>\n#include <string>\n#include <cstring>\n\n"
        )
        f.write(f"// Generated for {client_name} {latest_version}\n\n")

        # Namespace the header file.
        f.write("namespace netsender {\n\n")

        # Generate enums.
        f.write("enum var_type_t {\n")
        for i, type in enumerate(ENUM_TYPE_MAP):
            f.write(f"{ENUM_TYPE_MAP[type]} = {i},\n")
        f.write("};\n\n")

        f.write(f"constexpr const auto VAR_COUNT = {var_count};\n\n")

        # Generate an Enum for ID-based access.
        f.write("namespace netsender_var {\n")
        for var in vars:
            f.write(
                f'constexpr const auto VAR_ID_{var["name"].upper()} = "{var["name"]}";\n'
            )
        f.write("}\n\n")

        # Create an array to iterate through.
        f.write("constexpr const auto VARIABLES = std::array{\n")
        for var in vars:
            f.write(f"netsender_var::VAR_ID_{var['name'].upper()},\n")
        f.write("};\n\n")

        # Generate the State Struct to hold the variables.
        f.write("struct device_var_state_t {\n")
        for var in vars:
            c_type: str = C_TYPE_MAP.get(var["type"], "void*")
            if var["type"] == VarType.STRING:
                f.write(f"    char {var['name']}[64];\n")
            else:
                f.write(f"    {c_type} {var['name']};\n")
        f.write("};\n")

        # Add a helper function to parse the variables into the struct.
        f.write("""
            inline void update_state_member(device_var_state_t& state, int var_index, const std::string& val) {
            switch (var_index) {
            """)
        for i, var in enumerate(vars):
            f.write(f"case {i}:\n")
            match var["type"]:
                case VarType.BYTE:
                    f.write(
                        f"state.{var['name']} = static_cast<char>(std::stoi(val));\n"
                    )
                case VarType.STRING:
                    f.write(
                        f"strncpy(state.{var['name']}, val.c_str(), sizeof(state.{var['name']}) - 1);\n"
                    )
            f.write("break;\n")
        f.write("};\n};\n")

        f.write("} // namespace netsender\n")

        # Note: f.close() is not needed when using a 'with' context manager,
        # but leaving it in to match your original script's behavior.
        f.close()


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: python3 {sys.argv[0]} <input_json_path> <output_header_path>")
        sys.exit(1)

    generate_header(sys.argv[1], sys.argv[2])
    astyle_format_file(sys.argv[2])
