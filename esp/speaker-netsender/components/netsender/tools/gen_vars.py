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
        INT32: str = "int32"

    # Map JSON types to enum values.
    ENUM_TYPE_MAP: dict[str, str] = {
        VarType.BYTE: "BYTE",
        VarType.STRING: "STRING",
        VarType.INT32: "INT32",
    }

    # Map JSON types to C types.
    C_TYPE_MAP: dict[str, str] = {
        VarType.BYTE: "char",
        VarType.STRING: "char*",
        VarType.INT32: "int32_t",
    }

    # Open the JSON file.
    with open(json_path, "r") as f:
        data: dict[str, Any] = json.load(f)

    # Extract the client.
    client_name: str = list(data.keys())[0]

    # Generate header based off most recent version.
    latest_version: str = list(data[client_name])[-1]

    # Get the registered variables.
    vars: list[dict[str, str]] = data[client_name][latest_version]["variables"]
    vs_var: dict[str, str] = {
        "name": "vs",
        "type": VarType.INT32,
    }
    vars.insert(0, vs_var)
    var_count: int = len(vars)

    with open(output_path, "w") as f:
        # Add includes.
        f.write(
            '#pragma once\n#include <array>\n#include <cstdio>\n#include <optional>\n#include <string>\n#include <cstring>\n\n#include "esp_err.h"\n#include "esp_log.h"\n\n'
        )
        f.write(f"// Generated for {client_name} {latest_version}\n\n")

        # Namespace the header file.
        f.write("namespace netsender {\n\n")

        # Export ICD version.
        f.write(f'const constexpr auto ICD_VERSION = "{latest_version}";\n\n')

        # Export max string variable length.
        f.write(f"const constexpr auto MAX_STR_VAR_LEN = 512;\n\n")

        # Generate enums.
        f.write("enum var_type_t {\n")
        for i, type in enumerate(ENUM_TYPE_MAP):
            f.write(f"{ENUM_TYPE_MAP[type]} = {i},\n")
        f.write("};\n\n")

        f.write(f"constexpr const auto VAR_COUNT = {var_count};\n\n")

        # Generate an Enum for ID-based access.
        f.write("namespace var {\n")
        for var in vars:
            f.write(
                f'constexpr const auto VAR_ID_{var["name"].upper()} = "{var["name"]}";\n'
            )
        f.write("}\n\n")

        # Create an array to iterate through.
        f.write("constexpr const auto VARIABLES = std::array{\n")
        for var in vars:
            f.write(f"var::VAR_ID_{var['name'].upper()},\n")
        f.write("};\n\n")

        # Generate the State Struct to hold the variables.
        f.write("struct device_var_state_t {\n")
        for var in vars:
            c_type: str = C_TYPE_MAP.get(var["type"], "void*")
            if var["type"] == VarType.STRING:
                f.write(f"    char {var['name']}[MAX_STR_VAR_LEN];\n")
            else:
                f.write(f"    {c_type} {var['name']};\n")
        f.write("};\n\n")

        # Add a helper function to parse the variables into the struct.
        f.write(
            "inline void update_state_member(device_var_state_t& state, const std::string& var_id, const std::string& val) {"
        )

        first = True
        for var in vars:
            prefix = "if" if first else "} else if"
            var_name_upper = var["name"].upper()

            f.write(f"{prefix} (var_id == var::VAR_ID_{var_name_upper}) {{\n")

            if var["type"] == VarType.BYTE:
                f.write(f"state.{var['name']} = static_cast<char>(std::stoi(val));\n")
            elif var["type"] == VarType.STRING:
                # Using strncpy and ensuring the last byte is always null for safety
                f.write(
                    f"strncpy(state.{var['name']}, val.c_str(), sizeof(state.{var['name']}) - 1);\n"
                )
                f.write(
                    f"state.{var['name']}[sizeof(state.{var['name']}) - 1] = '\\0';\n"
                )
            elif var["type"] == VarType.INT32:
                f.write(
                    f"state.{var['name']} = static_cast<int32_t>(std::stoi(val));\n"
                )
            first = False

        if not first:
            f.write("}\n")  # Close the last if/else block

        f.write("}\n")

        # Generate the automated write function
        f.write(
            "\ninline esp_err_t write_vars_to_file(const device_var_state_t &state, const std::string& file_path) {\n"
        )
        f.write('    FILE* fd = fopen(file_path.c_str(), "w");\n')
        f.write("    if (fd == NULL) return ESP_FAIL;\n\n")

        for var in vars:
            v_name = var["name"]
            v_id = f"var::VAR_ID_{v_name.upper()}"
            if var["type"] == VarType.BYTE:
                f.write(
                    f'if (fprintf(fd, "%s:%d\\n", {v_id}, (int)state.{v_name}) < 0) {{ fclose(fd); return ESP_FAIL; }}\n'
                )
            elif var["type"] == VarType.STRING:
                f.write(
                    f'if (fprintf(fd, "%s:%s\\n", {v_id}, state.{v_name}) < 0) {{ fclose(fd); return ESP_FAIL; }}\n'
                )
            elif var["type"] == VarType.INT32:
                f.write(
                    f'if (fprintf(fd, "%s:%ld\\n", {v_id}, state.{v_name}) < 0) {{ fclose(fd); return ESP_FAIL; }}\n'
                )

        f.write("\nfclose(fd);\n")
        f.write("return ESP_OK;\n")
        f.write("}\n")

        # Write the read function.
        f.write("""
                inline std::optional<device_var_state_t> read_vars_from_file(const std::string& file_path)
                {
                    device_var_state_t vars = {};

                    ESP_LOGI("NETSENDER_VARS", "opening file");
                    FILE* fd = fopen(file_path.c_str(), "r");
                    if (fd == NULL) {
                        return std::nullopt;
                    }
                    ESP_LOGI("NETSENDER_VARS", "file opened");

                    // This is longer than we need, however, we don't have a standard
                    // YET for how long a variable name can be.
                    static char out[2 * MAX_STR_VAR_LEN + 1];
                    char* value = NULL;
                    while (fgets(out, 2 * MAX_STR_VAR_LEN, fd) != nullptr) {
                        if (strlen(out) >= 2 * MAX_STR_VAR_LEN) {
                            printf("err line too long for buffer: '%s' (len: %d)\\n", out, strlen(out));
                            break;
                        }

                        // Remove any newline characters.
                        out[strcspn(out, "\\r\\n")] = '\\0';

                        // Find the seperator.
                        value = strchr(out, ':');
                        if (value == NULL) {
                            ESP_LOGE("NETSENDER_VARS", "unable to find sep (:)\\n");
                            break;
                        }
                        *value = '\\0'; // Terminate the first string (name).
                        value++; // Point to the start of the second string (value).

                        ESP_LOGI("NETSENDER_VARS", "got variable: %s = %s", out, value);
                        update_state_member(vars, out, value);
                    }

                    fclose(fd);
                    return vars;
                }
            """)

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
