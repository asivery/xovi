from dataclasses import dataclass, field
from argparse import ArgumentParser
from enum import Enum

class ParseState(Enum):
    Default = 0
    Metadata = 1

@dataclass
class Resource:
    name: str
    value: bytes

    def stringify(self):
        return f"const char r${self.name}[{len(self.value) + 1}] = {{ {','.join(map(str, self.value))}, 0 }};"
    def reference(self):
        return f"extern const char *r${self.name};"

    @classmethod
    def load(clazz, res_name, file_name):
        with open(file_name, 'rb') as e:
            return clazz(res_name, e.read())

list_field = lambda: field(default_factory=list)

@dataclass
class HeaderState:
    version: tuple[int, int, int] = None
    resources: list[Resource] = list_field()

    exports: list[str] = list_field()
    imports: list[str] = list_field()
    conditions: list[str] = list_field()
    overrides: list[str] = list_field()

    override_prefix: str = "override$"
    lang: str = "c"

    def emit_files(self):
        any_args = "" if self.lang == "c" else "..."

        names_table = []
        link_table = []
        imports = []
        deps = []

        for condition in self.conditions:
            names_table.append('C' + condition)
            link_table.append(0)

        for imp in self.imports:
            idx = len(link_table) + 1
            names_table.append('I' + imp)
            if '$' not in imp:
                imp = '$' + imp
            imports.append(f"#define {imp} ((unsigned long long int(*)({any_args})) LINKTABLEVALUES[{idx}])")
            link_table.append(0)

        for exp in self.exports:
            deps.append(f"extern void {exp}();")
            names_table.append('E' + exp)
            link_table.append(exp)

        for ovr in self.overrides:
            fn = self.override_prefix + ovr
            deps.append(f"extern void {fn}();")
            names_table.append('O' + ovr)
            link_table.append(fn)

        version = ""
        if self.version is not None:
            v = (self.version[0] << 16) | (self.version[1] << 8) | (self.version[2])
            version = f"""__attribute__((section(".xovi_info"))) const int EXTENSIONVERSION = {v};"""
        else:
            print('Warning: No version defined in the XOVI project file.')

        link_table.insert(0, len(link_table))
        zero = '\\0'
        return (
            f"""
// This file is autogenerated. Please do not alter it manually and instead run xovigen.py.
// XOVI extension / module base file

// Deps
{format_array(deps)}

// XOVI metadata
__attribute__((section(".xovi"))) const char *LINKTABLENAMES = "{format_array(names_table, '', '{}' + zero)}{zero}";
__attribute__((section(".xovi"))) const void *LINKTABLEVALUES[] = {{ {format_array(link_table, ', ', '(void *) {}')} }};
{version}

// Resources
{map_array(self.resources, lambda e: e.stringify())}

            """.strip() + '\n', 

            f"""
// XOVI project import / resource header file. This file is autogenerated. Do not edit.
#ifndef _XOVIGEN
#define _XOVIGEN

extern const void *LINKTABLEVALUES[];

// Imports
{format_array(imports)}

// Resources
{map_array(self.resources, lambda e: e.reference())}

#endif
            """.strip() + '\n'
        )


def format_array(array, separator='\n', fmt='{}'):
    return separator.join(fmt.format(x) for x in array)

def map_array(array, translator, separator='\n'):
    return separator.join(translator(x) for x in array)

def strip_split(string, delim=' '):
    if ';' in string: string = string[:string.rfind(';')]
    return [x.strip() for x in string.strip().split(delim)]

def parse_version_string(version_string):
    try:
        version_tokens = [int(x) for x in version_string.strip().split('.')]
        if len(version_tokens) != 3 or any(x > 255 or x < 0 for x in version_tokens):
            raise BaseException("invalid format")
    except BaseException:
        print(f"Warning: Invalid version format {version_string}. Use major.minor.patch (semver). Assuming 0.1.0")
        version_tokens = [0, 1, 0]
    return tuple(version_tokens)


def parse_xovi_file(file_lines):
    header = HeaderState()
    state = ParseState.Default
    for ln, line in enumerate(file_lines):
        err = lambda log: print(f'Error in line {ln+1}: {log}')
        line = strip_split(line)
        if len(line) == 1 and line[0] == '':
            continue
        if state is ParseState.Default:
            if len(line) != 2:
                err("Invalid number of tokens")
                return None
            keyword, argument = line
            match keyword.lower():
                case 'import':
                    header.imports.append(argument)
                case 'import?':
                    header.imports.append(argument)
                    header.conditions.append(argument)
                case 'condition':
                    header.conditions.append(argument)
                case 'export':
                    header.exports.append(argument)
                case 'override':
                    header.overrides.append(argument)
                case 'resource':
                    res_name, file = strip_split(argument, ':')
                    header.resources.append(Resource.load(res_name, file))
                case 'version':
                    header.version = parse_version_string(argument)

                # Non-standard directives
                case 'ns_setoverrideprefix':
                    header.override_prefix = argument

                case other:
                    err(f"Unknown directive: {other}")
                    return None

    return header


def main():
    argparse = ArgumentParser()
    argparse.add_argument('-o', '--output', help="Output xovi module base", required=True)
    argparse.add_argument('-H', '--output-header', help="Output xovi header")
    argparse.add_argument('input', help="The .xovi file defining all imports and exports of all the files in this project.")
    args = argparse.parse_args()

    with open(args.input, 'r') as definition:
        header = parse_xovi_file(definition.readlines())
        if header is None:
            return

    if args.output.endswith('cpp'):
        header.lang = "cpp"

    c, h = header.emit_files()
    with open(args.output, 'w') as output:
        output.write(c)
    if args.output_header is not None:
        with open(args.output_header, 'w') as output:
            output.write(h)


if __name__ == "__main__": main()
