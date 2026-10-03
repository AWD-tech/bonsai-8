"""Format the actual status template at full-width counter extremes."""
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

def status_format():
    source = (ROOT/'firmware/src/dual_firmware.inc').read_text()
    body = source[source.index('static void dual_status(void)'):source.index('static void dual_library(void)')]
    match = re.search(r'snprintf\(line,sizeof\(dd_ui_line\),\s*("(?:[^"\\]|\\.)*")\s*,(.*?)\);', body, re.S)
    if not match:
        raise AssertionError('The actual bounded status formatter is missing')
    literal, arguments = match.groups()
    args, depth, start = [], 0, 0
    for i, char in enumerate(arguments):
        if char == '(':
            depth += 1
        elif char == ')':
            depth -= 1
        elif char == ',' and depth == 0:
            args.append(arguments[start:i].strip()); start = i+1
    args.append(arguments[start:].strip())
    size = int(re.search(r'static char dd_ui_line\[(\d+)\]',source).group(1))
    return body, literal, args, size

class StatusFormatTests(unittest.TestCase):
    def test_all_maximum_counters_fit_and_remain_complete_json(self):
        body, literal, arguments, size = status_format()
        specs = re.findall(r'%(?:u|d)',literal)
        self.assertEqual(len(specs),len(arguments),'status arguments must exactly match fields')
        self.assertNotRegex(re.sub(r'%(?:u|d)', '',literal), r'%')
        values = ','.join('UINT_MAX' if spec == '%u' else 'INT_MIN' for spec in specs)
        code = ('#include <stdio.h>\n#include <limits.h>\n'
                f'int main(void){{char buffer[{size}];'
                f'int n=snprintf(buffer,sizeof(buffer),{literal},{values});'
                'if(n<=0||n>=(int)sizeof(buffer))return 1;'
                'fwrite(buffer,1,(size_t)n,stdout);return 0;}')
        with tempfile.TemporaryDirectory(prefix='bonsai-status-') as folder:
            cfile, binary = Path(folder)/'status.c', Path(folder)/'status'
            cfile.write_text(code)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(cfile),'-o',str(binary)],check=True)
            result = subprocess.run([str(binary)],capture_output=True,text=True,check=True)
        data = json.loads(result.stdout)
        self.assertLess(len(result.stdout.encode()),size)
        self.assertEqual(data['grid_bpm'],[4294967295]*2)
        self.assertEqual(len(data['gains']),8)
        self.assertEqual(data['record']['slot'],-2147483648)
        self.assertEqual(data['pitch_preserving'],1)
        self.assertTrue(result.stdout.endswith('}\n'))
        self.assertIn('Status response overflow',body,'even an unexpected longer formatter must fail closed')

    def test_status_reports_applied_dsp_tempo_instead_of_pending_request(self):
        _, _, arguments, _ = status_format()
        self.assertIn('dd_tempo_actual(&dd.deck[0])',arguments)
        self.assertIn('dd_tempo_actual(&dd.deck[1])',arguments)
        self.assertNotIn('atomic_load(&dd.deck[0].speed)',arguments)
        self.assertNotIn('atomic_load(&dd.deck[1].speed)',arguments)
