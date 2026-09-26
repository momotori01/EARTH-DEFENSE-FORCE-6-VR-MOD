"""Pull the HLSL out of eye_warp.cpp so it can be compiled without the game.

The shader is compiled at runtime, so a mistake in it does not fail the build:
it fails in the headset, as a line in the log. Running fxc over it here turns
that into a compile error at the desk instead.

  python extract_shader.py [output.hlsl]
"""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[2]) if len(sys.argv)>2 else pathlib.Path(__file__).resolve().parent.parent / 'src' / 'eye_warp.cpp'
text = source.read_text(encoding='utf-8')
start = text.index('R"(', text.index('kShader' if source.name=='eye_warp.cpp' else 'constexpr char shader[]')) + 3
end = text.index(')";', start)
out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'eye_warp.hlsl')
# Adjacent raw literals keep each token below MSVC's string-literal limit.
# Reproduce the compiler's concatenation rather than passing delimiters to fxc.
shader = re.sub(r'\)"\s*R"\(', '', text[start:end])
out.write_text(shader, encoding='utf-8')
print(out)
