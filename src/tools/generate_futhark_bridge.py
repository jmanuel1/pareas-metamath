#!/usr/bin/env python3
import json
import sys
import textwrap

input_file, output_file = sys.argv[1:]

manifest = json.loads(open(input_file).read())
types = manifest['types']


def is_scalar_type(type_):
    return type_ in ['bool', 'u8', 'i8', 'u16', 'i16', 'u32', 'i32', 'u64', 'i64']

def generate_projection(field):
    c_type = futhark_to_c_type_name[field['type']]
    sync = 'if (futhark_context_sync(this->ctx)) throw Error(this->ctx);' if is_scalar_type(field['type']) else ''
    return textwrap.dedent(f"""\
        {c_type} project_{field['name']}(void) const {{
            {c_type} out;
            int err = {field['project']}(this->ctx, &out, this->data);
            if (err != 0) {{
                throw Error(this->ctx);
            }}
            {sync}
            return out;
        }}""")

def generate_types(fields):
    return ', '.join(futhark_to_c_type_name[field['type']] for field in fields)

def generate_tuple(fields):
    return ', '.join(f'this->project_{field['name']}()' for field in fields)

futhark_to_c_type_name = {
    'bool': 'bool',
    'u32': 'uint32_t',
    'i32': 'int32_t',
    'i64': 'int64_t',
    'u64': 'uint16_t',
    'u16': 'uint16_t',
    'i16': 'int16_t',
    'u8': 'uint8_t',
    'i8': 'int8_t',
}
for futhark_type, type_ in types.items():
    futhark_to_c_type_name[futhark_type] = type_['ctype']

output = open(output_file, 'w')

guard_macro_name = '_PAREAS_COMPILER_FUTHARK_BRIDGE_' + input_file.split('.')[0]
output.write(textwrap.dedent(f"""\
    #ifndef {guard_macro_name}
    #define {guard_macro_name}
    namespace futhark {{"""))
for futhark_type, type_ in types.items():
    print('processing futhark type', futhark_type)
    ctype = type_['ctype']
    if 'record' not in type_:
        continue
    free_fn = type_['ops']['free']
    project_fns = type_['record']['fields']
    new_fn = type_['record']['new']

    classname = f'UniqueTuple_{ctype.removeprefix('struct futhark_opaque_').removesuffix(' *')}'
    code = textwrap.dedent(f"""\
        struct {classname} {{
            futhark_context* ctx;
            {ctype} data;

            {classname}(futhark_context* ctx, {ctype} data):
                ctx(ctx), data(data) {{
            }}

            explicit {classname}(futhark_context* ctx):
                ctx(ctx), data(nullptr) {{
            }}

            {classname}({classname}&& other):
                ctx(other.ctx), data(std::exchange(other.data, nullptr)) {{
            }}

            {classname}() = delete;

            {classname}& operator=({classname}&& other) {{
                std::swap(this->data, other.data);
                std::swap(this->ctx, other.ctx);
                return *this;
            }}

            {classname}(const {classname}&) = delete;
            {classname}& operator=(const {classname}&) = delete;

            ~{classname}() {{
                if (this->data) {{
                    {free_fn}(this->ctx, this->data);
                }}
            }}

            void clear() {{
                if (this->data) {{
                    {free_fn}(this->ctx, this->data);
                    this->data = nullptr;
                }}
            }}

            {ctype} get() {{
                return this->data;
            }}

            const {ctype} get() const {{
                return this->data;
            }}

            {ctype}* operator&() {{
                return &this->data;
            }}

            operator {ctype}() {{
                return this->data;
            }}

            operator const {ctype}() const {{
                return this->data;
            }}

            {''.join(generate_projection(field) for field in project_fns)}

            std::tuple<{generate_types(project_fns)}> to_tuple(void) const {{
                return {{{generate_tuple(project_fns)}}};
            }}
        }};

        """)
    output.write(code)
output.write(textwrap.dedent("""\
    }
    #endif"""))
