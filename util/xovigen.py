from dataclasses import dataclass, field
from argparse import ArgumentParser
from enum import Enum
from typing import Any
import os
import re

# from itertools import batched
from itertools import islice
def batched(iterable, n, *, strict=False):
    # batched('ABCDEFG', 3) → ABC DEF G
    if n < 1:
        raise ValueError('n must be at least one')
    iterator = iter(iterable)
    while batch := tuple(islice(iterator, n)):
        if strict and len(batch) != n:
            raise ValueError('batched(): incomplete batch')
        yield batch

class ParseState(Enum):
    Default = 0
    Metadata = 1


class MetadataType(Enum):
    Int = 1
    Bool = 2
    String = 3

class HeaderAdditionType(Enum):
    Condition = 1
    Import = 2
    Export = 3
    Override = 4

@dataclass(frozen=True)
class HeaderAddition:
    type_: HeaderAdditionType
    name: str

@dataclass
class Resource:
    name: str
    value: bytes

    def stringify(self):
        return f"volatile const char r${self.name}[{len(self.value) + 1}] = {{ {','.join(map(str, self.value))}, 0 }};"
    def reference(self):
        return f"extern const char r${self.name}[{len(self.value) + 1}];"

    @classmethod
    def load(clazz, res_name, file_name):
        with open(file_name, 'rb') as e:
            return clazz(res_name, e.read())

@dataclass
class MetadataEntry:
    name_offset: int
    type_: MetadataType
    value: Any

    def emit_value(self):
        if self.type_ is MetadataType.Int:
            return f".i = {self.value}"
        elif self.type_ is MetadataType.Bool:
            return f".b = {self.value}"
        elif self.type_ is MetadataType.String:
            return f".sLength = {self.value[0]}, .s = &__XOVIMETADATANAMES[{self.value[1]}]"

    def get_internal_name(self):
        return f"__XME{hex(self.name_offset)[2:].upper()}"

    def emit(self):
        return f"""
const struct XoviMetadataEntry {self.get_internal_name()} = {{
    .name = &__XOVIMETADATANAMES[{self.name_offset}],
    .type = {self.type_.value},
    .value = {{
        {self.emit_value()}
    }}
}};
""".strip() + "\n"

list_field = lambda: field(default_factory=list)
GLOBAL_METADATA = object()

@dataclass
class HeaderState:
    version: tuple[int, int, int] = None
    resources: list[Resource] = list_field()

    exports: list[str] = list_field()
    imports: list[str] = list_field()
    conditions: list[str] = list_field()
    overrides: list[str] = list_field()
    dependencies: list[tuple[str, int, int, int]] = list_field()
    metadata: dict[HeaderAddition, list[MetadataEntry]] = field(default_factory=dict)
    metadata_name_table = ''
    metadata_name_table_offset = 0

    override_prefix: str = "override$"
    lang: str = "c"

    def add_metadata_entry_for_entry(self, entry: HeaderAddition, name: str, type_: MetadataType, value: Any):
        if entry not in self.metadata:
            self.metadata[entry] = []
        name_offset = self.metadata_name_table_offset
        self.metadata_name_table_offset += len(name) + 1
        self.metadata_name_table += name + "\\0"
        # If metadata value is a string, add it to the name table too, then remap
        if type_ is MetadataType.String:
            output_value = str(value) if type(value) is not bytes else ''.join('\\x' + ('0' if e < 0x10 else '') + hex(e)[2:] for e in value)
            value = (len(value), self.metadata_name_table_offset)
            self.metadata_name_table_offset += value[0] + 1
            self.metadata_name_table += output_value + "\\0"
        self.metadata[entry].append(MetadataEntry(name_offset, type_, value))

    def check_dependency(self, referenced_global: str):
        if '$' in referenced_global:
            parent = referenced_global[:referenced_global.find('$')]
            for x in self.dependencies:
                if x[0] == parent:
                    break
            else:
                raise BaseException(f"Error: Cannot reference {referenced_global} from {parent} without adding an explicit dependency with the 'depends-on' statement before the import!")

    def get_metadata_junction_name(self, owner: HeaderAddition, value: list[MetadataEntry]):
        if len(value) == 0:
            return None
        if owner is GLOBAL_METADATA:
            return "__XMJroot"
        else:
            return "__XMJ" + value[0].get_internal_name()

    def get_reference_to_metadata(self, entry: HeaderAddition):
        if entry not in self.metadata:
            return "(const struct XoviMetadataEntry **) 0"
        else:
            return '(const struct XoviMetadataEntry **) &' + self.get_metadata_junction_name(entry, self.metadata[entry])


    def emit_metadata_junction(self, owner: HeaderAddition, value: list[MetadataEntry]):
        if len(value) == 0:
            return ""
        own_name = self.get_metadata_junction_name(owner, value)
        return f"const struct XoviMetadataEntry *{own_name}[] = {{ {', '.join('&' + a.get_internal_name() for a in value)}, (struct XoviMetadataEntry *) 0 }};"

    def create_import(self, statement: str, idx: int):
        any_args = "" if self.lang == "c" else "..."
        name, *params = re.split("([:>])", statement)
        args = []
        ret = "unsigned long long int"
        for type_, val in batched(params, 2):
            if type_ == ':':
                args.append(val)
            elif type_ == '>':
                ret = val
        name_func = name.replace('-', '_')
        return f"#define {name_func} (({ret} (*)({', '.join(args) or any_args})) LINKTABLEVALUES[{idx}])", name


    def emit_files(self):
        names_table = []
        link_table = []
        imports = []
        deps = []
        metadata_table = [GLOBAL_METADATA]

        for condition in self.conditions:
            _, condition = self.create_import(condition, -1)
            names_table.append('C' + condition)
            link_table.append(0)
            metadata_table.append(HeaderAddition(HeaderAdditionType.Condition, condition))

        for imp in self.imports:
            idx = len(link_table) + 1
            names_table.append('I' + self.create_import(imp, idx)[1])
            if '$' not in imp:
                imp = '$' + imp
            import_, name = self.create_import(imp, idx)
            imports.append(import_)
            link_table.append(0)
            metadata_table.append(HeaderAddition(HeaderAdditionType.Import, name))

        for exp in self.exports:
            deps.append(f"extern void {exp}();")
            names_table.append('E' + exp)
            link_table.append(exp)
            metadata_table.append(HeaderAddition(HeaderAdditionType.Export, exp))

        for ovr in self.overrides:
            fn = self.override_prefix + ovr
            deps.append(f"extern void {fn}();")
            names_table.append('O' + ovr)
            link_table.append(fn)
            metadata_table.append(HeaderAddition(HeaderAdditionType.Override, ovr))

        version = ""
        if self.version is not None:
            v = (self.version[0] << 16) | (self.version[1] << 8) | (self.version[2])
            version = f"""__attribute__((section(".xovi_info"))) const int EXTENSIONVERSION = {v};"""
        else:
            print('Warning: No version defined in the XOVI project file.')

        link_table.insert(0, len(link_table))
        zero = '\\0'
        nl_tab_sep = ',\n    '
        return (
            f"""
// This file is autogenerated. Please do not alter it manually and instead run xovigen.py.
// XOVI extension / module base file
#ifdef __cplusplus
extern "C" {{
#endif
{GLOBAL_HEADER}

// Deps
{format_array(deps)}

// XOVI metadata
__attribute__((section(".xovi"))) const char *LINKTABLENAMES = "{format_array(names_table, '', '{}' + zero)}{zero}";
__attribute__((section(".xovi"))) const void *LINKTABLEVALUES[] = {{ {format_array(link_table, ', ', '(void *) {}')} }};
__attribute__((section(".xovi"))) const struct XoViEnvironment *Environment = 0;
{version}

__attribute__((section(".xovi_info"))) const char __XOVIMETADATANAMES[] = "{self.metadata_name_table}";

// Raw Metadata Entries
{map_array((x for y in self.metadata.values() for x in y), lambda e: e.emit())}

// Metadata Chains
{map_array(self.metadata.items(), lambda e: self.emit_metadata_junction(e[0], e[1]))}

// Main metadata list
__attribute__((section(".xovi"))) const struct XoviMetadataEntry **METADATAVALUES[] = {{
    {map_array(metadata_table, lambda e: self.get_reference_to_metadata(e), nl_tab_sep)}, (const struct XoviMetadataEntry **) 1
}};

// Resources
{map_array(self.resources, lambda e: e.stringify())}

// Dependency constructor
void _xovi_depconstruct() {{
    {map_array(self.dependencies, lambda e: f"Environment->requireExtension{e!r};".replace("'", '"'))}
}}

#ifdef __cplusplus
}}
#endif
            """.strip() + '\n', 

            f"""
// XOVI project import / resource header file. This file is autogenerated. Do not edit.
#ifndef _XOVIGEN
#define _XOVIGEN
#ifdef __cplusplus
extern "C" {{
#endif
{GLOBAL_HEADER}

extern const void *LINKTABLEVALUES[];

// Imports
{format_array(imports)}

// Resources
{map_array(self.resources, lambda e: e.reference())}

// Environment
extern const struct XoViEnvironment *Environment;

#ifdef __cplusplus
}}
#endif
#endif
            """.strip() + '\n'
        )


def format_array(array, separator='\n', fmt='{}'):
    return separator.join(fmt.format(x) for x in array)

def map_array(array, translator, separator='\n'):
    return separator.join(translator(x) for x in array)

def strip_split(string, delim=' '):
    if ';' in string: string = string[:string.rfind(';')]
    return [x.strip() for x in string.strip().split(delim) if x]

def parse_version_string(version_string):
    try:
        version_tokens = [int(x) for x in version_string.strip().split('.')]
        if len(version_tokens) != 3 or any(x > 255 or x < 0 for x in version_tokens):
            raise BaseException("invalid format")
    except BaseException:
        print(f"Warning: Invalid version format {version_string}. Use major.minor.patch (semver). Assuming 0.1.0")
        version_tokens = [0, 1, 0]
    return tuple(version_tokens)


def parse_xovi_file(file_lines, architecture):
    header = HeaderState()
    state = ParseState.Default
    metadata_attribution = None

    for ln, line in enumerate(file_lines):
        err = lambda log: print(f'Error in line {ln+1}: {log}')
        if state is ParseState.Default:
            line = strip_split(line)
            if len(line) == 0:
                continue
            # Metadata-special tokens:
            if line[0] in ('global-meta', 'with'):
                line = [line[0], None]

            if len(line) != 2:
                err("Invalid number of tokens")
                return None
            keyword, argument = line
            match keyword.lower():
                case 'depends-on':
                    module, semver = argument.split(':')
                    mj, mn, pa = map(int, semver.split('.'))
                    header.dependencies.append((module, mj, mn, pa))
                case 'import':
                    header.check_dependency(argument)
                    header.imports.append(argument)
                    metadata_attribution = HeaderAddition(HeaderAdditionType.Import, argument)
                case 'import?':
                    header.check_dependency(argument)
                    header.imports.append(argument)
                    header.conditions.append(argument)
                    metadata_attribution = HeaderAddition(HeaderAdditionType.Import, argument)
                case 'condition':
                    header.conditions.append(argument)
                    metadata_attribution = HeaderAddition(HeaderAdditionType.Condition, argument)
                case 'export':
                    header.exports.append(argument)
                    metadata_attribution = HeaderAddition(HeaderAdditionType.Export, argument)
                case 'override':
                    header.overrides.append(argument)
                    metadata_attribution = HeaderAddition(HeaderAdditionType.Override, argument)
                case 'resource':
                    res_name, file = strip_split(argument, ':')
                    header.resources.append(Resource.load(res_name, file))
                    metadata_attribution = None
                case 'version':
                    header.version = parse_version_string(argument)
                    metadata_attribution = None
                case 'global-meta':
                    state = ParseState.Metadata
                    metadata_attribution = GLOBAL_METADATA
                case 'with':
                    if metadata_attribution is None:
                        err(f"Cannot use 'with' in this context! No metadata attribution defined!")
                        return None
                    state = ParseState.Metadata


                # Non-standard directives
                case 'ns_setoverrideprefix':
                    header.override_prefix = argument

                case other:
                    err(f"Unknown directive: {other}")
                    return None
        elif state is ParseState.Metadata:
            # Metadata line format:
            # name = value
            # Where value is one of <number (1234)>, <boolean (true/false)>, <file (:test.txt)>, <string ("Hello!")>
            line = line.strip()
            if not line: continue
            if line == 'end':
                metadata_attribution = None
                state = ParseState.Default
                continue
            if ' = ' not in line:
                err("Invalid format!")
                return None
            split_idx = line.find(' = ')
            name_part = line[:split_idx]
            if '.' in name_part:
                # Architecture dependent
                if architecture is None:
                    err("This extension requires the -a (--architecture) option! Please set it!")
                    return None
                arch_req, name_part = name_part.split('.')
                if arch_req.lower() != architecture.lower():
                    continue
            data_part = line[split_idx + 3:]
            if data_part.isdecimal():
                header.add_metadata_entry_for_entry(metadata_attribution, name_part, MetadataType.Int, data_part)
            elif data_part.lower() in ('true', 'false'):
                header.add_metadata_entry_for_entry(metadata_attribution, name_part, MetadataType.Bool, data_part.lower())
            elif data_part.startswith(':'):
                with open(data_part[1:], 'rb') as file_data:
                    header.add_metadata_entry_for_entry(metadata_attribution, name_part, MetadataType.String, file_data.read())
            elif data_part.startswith('"') and data_part.endswith('"'):
                header.add_metadata_entry_for_entry(metadata_attribution, name_part, MetadataType.String, data_part[1:-1])
            else:
                err("Invalid metadata value!")

    return header

GLOBAL_HEADER = None
def main():
    global GLOBAL_HEADER
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../src/external.h"), 'r') as e:
        GLOBAL_HEADER = e.read()
    argparse = ArgumentParser()
    argparse.add_argument('-o', '--output', help="Output xovi module base", required=True)
    argparse.add_argument('-H', '--output-header', help="Output xovi header")
    argparse.add_argument('-a', '--architecture', help="The architecture for some arch-dependent metadata fields")
    argparse.add_argument('input', help="The .xovi file defining all imports and exports of all the files in this project.")
    args = argparse.parse_args()

    with open(args.input, 'r') as definition:
        header = parse_xovi_file(definition.readlines(), args.architecture)
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
