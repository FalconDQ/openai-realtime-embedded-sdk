from pathlib import Path
import subprocess
import tempfile
base=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as temp:
    binary=Path(temp)/'rtp-test'
    subprocess.run(['gcc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    str(base/'payload_test.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('RTP extensions/padding/malformed-packet regression tests passed.')
