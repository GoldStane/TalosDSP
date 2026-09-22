import subprocess
import sys

for args in [('--wet', 'nan'), ('--wet', 'inf'), ('--wet', '2'),
             ('--channels', '3'), ('--blocksize', '999999999999999999999'),
             ('--chain', 'typo'), ('--watchdog', 'typo'), ('--classifier', 'typo'),
             ('--pid-kp', 'nan')]:
    result = subprocess.run([sys.argv[1], *args], capture_output=True, timeout=5)
    assert result.returncode == 2, (args, result.returncode, result.stderr)
