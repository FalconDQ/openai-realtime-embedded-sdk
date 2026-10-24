from pathlib import Path
import subprocess
import tempfile
base = Path(__file__).resolve().parent
project = base.parent.parent
# Real ring source, fake allocation guard bytes, randomized FIFO traffic.
with tempfile.TemporaryDirectory() as temp:
    binary = Path(temp)/'buffer-test'
    subprocess.run(['gcc', '-O0', '-fno-builtin', '-I', str(base),
        '-I', str(project/'deps/libpeer/src'), str(base/'harness.c'),
        str(project/'components/peer/overrides/buffer.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Ringbuffer: 50,000 randomized operations and 10,000 boundary cycles passed.')
