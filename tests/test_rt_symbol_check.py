import importlib.util
import pathlib
import unittest
import sys
sys.dont_write_bytecode = True
from unittest.mock import patch

path = pathlib.Path(__file__).resolve().parents[1] / 'tools/rt-safety/rt_symbol_check.py'
spec = importlib.util.spec_from_file_location('rt_check', path)
rt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rt)

class SymbolParserTests(unittest.TestCase):
    def test_apple_bare_symbols(self):
        with patch.object(rt.sys, 'platform', 'darwin'):
            symbols = rt.parse_nm_output('_malloc\n__Znwm\n___cxa_throw\n_memcpy\n')
        self.assertEqual(symbols, ['malloc', '_Znwm', '__cxa_throw', 'memcpy'])
        for symbol in symbols[:3]:
            self.assertTrue(rt.check_symbol(symbol, 'fixture.o'))
        self.assertFalse(rt.check_symbol('memcpy', 'fixture.o'))

    def test_gnu_symbols(self):
        with patch.object(rt.sys, 'platform', 'linux'):
            symbols = rt.parse_nm_output('                 U malloc\n                 U _Znwm\n')
        self.assertEqual(symbols, ['malloc', '_Znwm'])
        for symbol in symbols:
            self.assertTrue(rt.check_symbol(symbol, 'fixture.o'))

    def test_unknown_external_is_rejected(self):
        self.assertTrue(rt.check_symbol('unknown_external', 'fixture.o'))

if __name__ == '__main__':
    unittest.main()
