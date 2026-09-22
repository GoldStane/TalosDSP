"""Compile actual object fixtures; a forbidden allocation must fail the audit."""
from pathlib import Path
import subprocess
import sys
import tempfile

compiler, checker = sys.argv[1:]
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    fixtures = {
        'safe': '#include <cstring>\nextern "C" void probe(void* out, const void* in, unsigned n) { std::memcpy(out,in,n); }\n',
        'unsafe': '#include <cstdlib>\nextern "C" void* probe(unsigned n) { return std::malloc(n); }\n',
    }
    for name, source in fixtures.items():
        src, obj = root / (name+'.cpp'), root / (name+'.o')
        src.write_text(source)
        subprocess.run([compiler, '-std=c++17', '-O0', '-fno-exceptions', '-c', str(src), '-o', str(obj)], check=True)
        result = subprocess.run([sys.executable, checker, str(obj)], capture_output=True, text=True)
        assert result.returncode == (0 if name == 'safe' else 1), result.stdout + result.stderr
        if name == 'unsafe': assert 'malloc' in result.stdout, result.stdout
